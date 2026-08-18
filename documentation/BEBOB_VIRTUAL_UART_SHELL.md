# BridgeCo BeBoB Virtual UART & Interactive Diagnostic Shell

## 1. Overview & Architectural Significance

The **BridgeCo DM1000 / DM1001 / DM1500** audio chipset family (used across M-Audio FireWire 1814, ProjectMix I/O, 410, Solo, Terratec Phase 88, PreSonus FireBox/FireStudio, and the original Focusrite Saffire) runs an embedded real-time operating system: **KnOS**, BridgeCo's own kernel.

> **The RTOS is KnOS, not ThreadX.** Earlier revisions of this document asserted
> Express Logic ThreadX. That is wrong, and the firmware image settles it:
> `sys ver` reports `KnOS 2.4 (Release)`, the image carries the build path
> `D:\projects\release\release_fw106_branch-20040622-01\dm1\system\KnOS\core\check_asserts.c`
> — KnOS is a subdirectory of BridgeCo's own source tree, alongside `AVC_V2_1`
> and `GTS` — and the image contains **zero** occurrences of "ThreadX" or
> "Express Logic" and **zero** `tx_*` symbols. `os th`'s
> priority / preemption-threshold / time-slice triples look ThreadX-like because
> that is a conventional RTOS thread table, not because ThreadX is underneath.
> **[measured]**

During production and factory bring-up, BridgeCo implemented an in-band diagnostic shell over IEEE 1394 asynchronous block transactions, mapped to `/dev/uart1394`. This interface exposes low-level silicon status, hardware FIFO depths, DCO/PLL calibration states, isochronous framer error latches, and RTOS task statistics.

In ASFireWire (ASFW), this subsystem provides **silicon-level observability** directly from the hardware, resolving the "black box" nature of FireWire audio stream bring-up.

```
                           ┌──────────────────────────────────────────────┐
                           │            KnOS 2.4 RTOS Core                │
                           │   (Tasks: ClockMgr, StreamEngine, Shell)     │
                           └──────────────────────┬───────────────────────┘
                                                  │
                                   stdout / stdin │ FIFO
                                                  ▼
                           ┌──────────────────────────────────────────────┐
                           │      BridgeCo 1394 Async Mailbox Engine      │
                           │   (AddrRegReq: 0xFFFF_C802_1000 / 9000)      │
                           └──────────────────────▲───────────────────────┘
                                                  │
                                IEEE 1394 Async Block R/W
                                                  │
                    ┌─────────────────────────────┴─────────────────────────────┐
                    ▼                                                           ▼
       ┌─────────────────────────┐                                 ┌─────────────────────────┐
       │   SwiftUI Diagnostics   │                                 │    MCP Control Plane    │
       │    (ASFW BeBoB Shell)   │                                 │ (Agent Diagnostic Tools)│
       └─────────────────────────┘                                 └─────────────────────────┘
```

---

## 2. Hardware Register Architecture & Wire Protocol

The Virtual UART communicates via four 48-bit FireWire address registers in the DM1000 bootloader/mailbox window:

| Address (High:Low) | Name | Access | Width / Format | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| `0xFFFF:C802_1000` | `AddrRegReq` | Block Write | 12 Bytes (3 Quadlets) | Request Command Envelope |
| `0xFFFF:C802_1040` | `AddrRegReqBuf` | Block Write | $N$ Bytes (max 1024) | Request Payload Buffer (stdin text) |
| `0xFFFF:C802_9000` | `AddrRegResp` | Block Read | 12 Bytes (3 Quadlets) | Response Status Envelope |
| `0xFFFF:C802_9040` | `AddrRegRespBuf` | Block Read | $M$ Bytes (max 1024) | Response Payload Buffer (stdout text) |

### 2.1 12-Byte Envelope Framing

All envelope quadlets are transmitted in **Little-Endian** byte order:

```
 Quadlet 0 (Offset 0x00):
  31                                  0
 ┌────────────────────────────────────┐
 │      protocolVersion (always 1)    │
 └────────────────────────────────────┘

 Quadlet 1 (Offset 0x04):
  31        24 23       16 15         0
 ┌────────────┬───────────┬───────────┐
 │ operandSize│   opcode  │ commandId │
 └────────────┴───────────┴───────────┘

 Quadlet 2 (Offset 0x08):
  31                                  0
 ┌────────────────────────────────────┐
 │         operand (byte count)       │
 └────────────────────────────────────┘
```

### 2.2 Opcode Taxonomy & Safety Interlocks

The DM1000 mailbox multiplexes both flash bootloader routines and the runtime virtual UART. **Strict opcode filtering is enforced in ASFW:**

| Opcode | Mnemonic | Classification | Safety Action | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x07` | `kSwitchTo1394Shell` | Virtual UART | **Permitted** | Connects shell console to `/dev/uart1394`. |
| `0x08` | `kReadShellChars` | Virtual UART | **Permitted** | Requests $M$ characters from stdout FIFO. |
| `0x09` | `kWriteShellChars` | Virtual UART | **Permitted** | Injects $N$ characters from `0x1040` into shell. |
| `0x04` | `kDownloadStart` | Flash Bootloader | **FORBIDDEN** | Initiates firmware flash write. |
| `0x05` | `kDownloadBlock` | Flash Bootloader | **FORBIDDEN** | Flashes raw image block to ROM. |
| `0x06` | `kDownloadEnd` | Flash Bootloader | **FORBIDDEN** | Commits and reboots into newly flashed image. |
| `0x0a` | `kProgramGUID` | Flash EEPROM | **FORBIDDEN** | Permanently overwrites EUI-64 GUID. |
| `0x0b` | `kProgramMAC` | Flash EEPROM | **FORBIDDEN** | Permanently overwrites Ethernet MAC. |
| `0x10` | `kProgramHWId` | Flash EEPROM | **FORBIDDEN** | Permanently overwrites Hardware ID/Revision. |
| `0x11` | `kStartFirmware` | Bootloader Cue | **Driver Internal** | Cues application boot (bootloader only). |

---

## 3. Firmware Reverse Engineering (IDA Pro Symbols)

Decompilation of the BridgeCo DM1000 firmware binary (`FW1814_appl_2.0.4.bcd` / ELF) reveals the inner mechanics of the shell and clock subsystem:

### 3.1 `Mailbox1394_TaskEntry` (`0x200B8D34`)
* **Role:** The KnOS mailbox polling task.
* **Mechanism:** Listens for 1394 block writes at `0xC802_1000`. Upon receiving Opcode `0x09`, it reads the byte count from operand quadlet 2, copies the characters from `0xC802_1040` into the shell line buffer, and wakes `Shell_DispatchCommand`.

### 3.2 `Shell_DispatchCommand` (`0x200CBE10`)
* **Role:** Master command table parser.
* **Available Command Trees:**
  * `sys`: System, streaming, memory, LLC, and hardware diagnostics.
  * `fw`: 1394 bus, sync manager, and isochronous connection inspection.
  * `os`: KnOS task list, stack usage, semaphores, and byte pools.
  * `help`: Enumerates all available commands and subcommands.

### 3.3 Diagnostic Functions of Critical Interest
* **`cmd_sys_stat_dump_streaming_stats` (`0x200E1E64` / `sys stat`):**
  * Outputs raw isochronous receiver statistics: `rxPackets`, `onlyHeaders`, `rxEmptyPkt`, `rxNoMem`, `rxQFillLevel`, `CtrDiffErr`, `SytDiffErr`, `BCOHdrErr`, `pkt Future`, `pkt Past`, `pktSytDiff`, `SytOffset`, `SytCorr`.
* **`cmd_sys_avstat_handler` (`0x201222F8` / `sys avstat all`):**
  * Dumps hardware silicon latches: `SetTgInLock` (TGEN audio clock PLL lock), `CIPMismatch`, `DBCMismatch`, `HeaderMismatch`.
* **`StreamSync_PrintState` (`0x2011EC00` / `fw sync show`):**
  * Dumps the master audio runtime context: Sample rate (32k–192k), active Iso channels (LineIn, SpdifAdatIn, SpdifAdatOut, MixerOut), Sync Source (Internal, ADAT Ext, S/PDIF Ext, Word Clock), Audio State (`0=Stop`, `1=Idle`, `2=Waiting for sync`, `3=Running`).
* **`sub_200F8E30` & `sub_200FCA98` (Clock & Sync State Machine):**
  * Manages clock switching and Digital PLL calibration.

---

## 4. Comparison with FFADO and Vendor Drivers

| Feature | Linux FFADO | Vendor Kext (Apple/M-Audio) | ASFW (This Project) |
| :--- | :--- | :--- | :--- |
| **Bootloader Information** | Yes (`bebob_firmware.cpp`) | Yes | Yes (`BeBoBBootloaderClient.cpp`) |
| **Bootloader Cue (`0x11`)** | Yes (`bebob_avdevice.cpp`) | Yes | Yes (`BeBoBBootloaderCue.cpp`) |
| **Safety Interlocks** | Minimal | Closed source binary | **Strict compile-time & runtime AST gates** |
| **1394 Virtual UART Client** | ❌ None | ❌ Removed in production | **✅ Fully implemented in C++ driver** |
| **Interactive Terminal UI** | ❌ None | ❌ None (CLI internal tool only) | **✅ SwiftUI Monospace Terminal & Dashboard** |
| **AI / MCP Control Plane** | ❌ None | ❌ None | **✅ 4 JSON-RPC tools for autonomous agents** |

### 4.1 No open-source stack has ever opened this shell

The "❌ None" above is checkable, not rhetorical. In libffado 2.5.0 the three
shell opcodes exist only as **enum names with zero call sites**:

```
$ grep -rn "SwitchTo1394Shell\|ReadShellChars\|WriteShellChars" references/libffado-2.5.0/
references/libffado-2.5.0/src/bebob/bebob_dl_codes.h:45:  eCmdC_SwitchTo1394Shell = 0x07,
references/libffado-2.5.0/src/bebob/bebob_dl_codes.h:46:  eCmdC_ReadShellChars    = 0x08,
references/libffado-2.5.0/src/bebob/bebob_dl_codes.h:47:  eCmdC_WriteShellChars   = 0x09,
```

Three declarations, nothing else in the tree. Linux's in-kernel
`sound/firewire/bebob/` mentions neither a shell nor a UART at all, and the ALSA
userspace control crate has no reference to `uart1394` or the shell opcodes.
FFADO inherited `bebob_dl_codes.h` from FreeBoB, so the names have sat unused in
both lineages since 2005. Somebody transcribed BridgeCo's opcode list and never
wired it up.

The practical consequence: the diagnostics in §8 of `1814.md` — the device's own
verdict on the stream it is receiving from us, its silicon latches, its clock
dividers, its mixer routing — were not available to any prior open-source
FireWire audio effort. Several 1814 bring-up questions that had no answer from
the wire were answered by asking the device.

---

### 4.2 Reaching the shell on other BridgeCo devices

**Nothing in ASFW's Virtual UART path is 1814-specific.** The client
(`ASFWDriver/Audio/Families/BeBoB/VirtualUart/`) is addressed by route token, and
the four `asfw_bebob_*` MCP tools take an explicit `nodeId` / `generation`, so
pointing them at a different BridgeCo unit needs no code change.

What is generic, and what is not:

| | status |
|---|---|
| Mailbox registers `0xFFFF_C802_1000` / `_1040` / `_9000` / `_9040` | **Generic BridgeCo bootloader window**, documented for the whole BeBoB family in `references/libffado-2.5.0/src/bebob/bebob_dl_mgr.cpp:45-49`. **[derived]** |
| Opcodes `0x07` / `0x08` / `0x09` | **Generic** — they live in BridgeCo's family-wide command enum (`bebob_dl_codes.h:45-47`), not in any vendor-specific table. **[derived]** |
| Application firmware hooking `/dev/uart1394` | **[measured] on the 1814**, and *expected* rather than surprising — see §4.3. |
| The same on Phase 88 / ProjectMix / FireBox / Saffire (original) | **[unverified]**, but with a cheap test: §4.3 gives two thread names and three module names whose presence predicts it. |
| Command *set* (`fw mix`, `sys avstat`, `/cfg` tree) | **[unverified] off the 1814.** `fw mix`'s input names (`i14s1..i14s7`, `linein1..4`) are clearly this product's I/O, so expect the trees to differ even where the shell exists. |

Probe order for a new device, cheapest and safest first:

1. Read the bootloader info block at `0xFFFF_C802_0000` (already implemented,
   read-only) to confirm the mailbox answers at all and to learn its protocol
   version. A device whose info block reads back is a candidate.
2. Send opcode `0x07` (`SwitchTo1394Shell`), then drain `0x9040`. On the 1814
   this yields the historical boot log, which is itself the module inventory.
3. Write `"\r\n"` — **quadlet-padded**, see §5.5 — and drain. A prompt means the
   shell is live; silence means the image has no shell task.
4. Only then try `help`.

Two cautions carried over from the 1814 work. The mailbox is **single
occupancy**: overlapping conversations get `rCode 4` and mispaired drains (§5.2).
And every opcode outside `0x07`/`0x08`/`0x09` stays **forbidden** — the flash and
EEPROM programming opcodes share this window (§2.2), which is exactly why the
opcode filter is compile-time rather than advisory.

---

### 4.3 Why the shell is reachable outside bootloader mode

Calling `0xFFFF_C802_xxxx` "the bootloader window" is the wrong mental model, and
it makes application-mode access look like a loophole. It is the DM1000's **async
command window**; the bootloader is one client of it and the shell console is
another.

Three pieces of evidence, all **[measured]**:

1. **`0x07` is `SwitchTo1394Shell` — a switch, not a start.** The console already
   exists; the opcode only re-points its stdio at the 1394 transport. The image
   carries `/dev/uart1`, `/dev/uart2`, `/dev/uartCombo` and `uart1394` as peer
   device nodes (with `uart1394Rx%d` / `uart1394Tx%d` / `uart1394TxAccess`
   plumbing) beneath a shared `TtyFileHandle` / `ttyBuffer` layer — and `/cfg/dev`
   lists all of them side by side. On a factory bench you would reach the same
   shell over the physical UART pins.

2. **Both relevant threads are resident in booted application firmware.** `os th`
   on a streaming 1814 shows

   ```
    8 cmdline_setupThread 24/24/1 rdy   13394  1387  2048 0x1000c780
   13 bldCmdProcessThread 24/24/0   q    4205   279  1024 0x1000e708
   ```

   `bldCmdProcessThread` — the bootloader *command* processor — is a permanent
   service thread, which is why the mailbox answers in application mode at all.
   `cmdline_setupThread` is the console, running since power-on. Neither is
   conditional on bootloader state.

3. **What the shell reports only exists while streaming.** `sys stat`'s per-stream
   isochronous counters, `sys avstat`'s framer/TGEN latches, `fw show`'s
   `Audio State = Running`, `fw mix`, `fw vol peak`, and
   `/cfg/dev/isodrv/ClockDividers` are all meaningless in a bootloader that has no
   isoch engine, no mixer and no locked PLL. A diagnostic console restricted to
   bootloader mode could not report any of it. The command set is *designed* for
   the running device.

Corollary for §4.2: the gating question for another BridgeCo device is not
"does the mailbox answer" but **"did this vendor's build link the console and
command modules"**. The boot log's `mod:` lines are the inventory — look for
`modBmCommand`, `modOsCommand`, `modDm1001Commands` — and `os th` should show
`cmdline_setupThread`. Their absence predicts a silent shell; their presence
predicts a working one.

Corollary for safety: the flash and EEPROM opcodes are hazardous **because that
handler is always listening**, not because application mode is trespassing. The
compile-time opcode filter is the load-bearing protection, in every device state.

---

### 4.4 Authoritative command inventory (from the firmware's own help strings)

Extracted from the image rather than by typing `help` at the device, so this is
complete and includes the stubs. **[measured]** — these are the literal help texts
the firmware carries.

### `sys` — diagnostics

| read-only | what |
|---|---|
| `sys help` `sys ver` `sys info` | identity and this text |
| `sys stat` | iso rx/tx counters (§8.1 of `1814.md`) |
| `sys avstat [clr\|tgen\|av1\|av2\|av\|frm\|mdb\|llc\|all\|full\|short]` | silicon latches; `clr` prints **and clears** |
| `sys tgen` `sys av` `sys frm` | timing generator, AV ports, framer block dump |
| `sys llc` | link layer config **and iso rx routing table** — never run |
| `sys mdbsettings` `sys mdbdump` | MDB settings and dump — never run |
| `sys isodrv` | **"avdrv internal state variables"** — never run; best lead for the `sytlog` gate |
| `sys swmtx` | switch matrix settings (pin muxing) |
| `sys gpio` | "gpio status **and manipulation**" — read side only |
| `sys sytlog` | "shows log of received packets **if enabled in the streaming driver**" |

**Mutating:** `sys reset` (resets the system).

**Stubs — "To Be Implemented", do not waste time on them:** `sys dma`, `sys pic`,
`sys cp15`, `sys clock`, `sys uart`, `sys spi`, `sys memscan`, `sys memscanraw`,
`sys memdump`, `sys mdbreset`, `sys frmrd`, `sys frmwr`.

### `fw` — audio engine, mixer, clock

```
fw mix reset | show | setup1 (Line1-2 -> Line1-2)
fw mix connect|disconnect <input> <output>
fw mix connect|disconnect headphone <output>
fw aux show | connect <source> <sink>     source: aux, main   sink: line1, line2, spdif
fw vol show | peak | dac | set <block> <level> [channel]     level: -128...0 dB
fw sync show | set <source>               source: opt, rca, internal opt, internal rca
fw show
```

Note the help text's own typo in the mixer input list — it prints `lin2in2` where
the working name is `linein2`. The names that actually resolve are the ones
`fw mix show` echoes: `linein1..4, spdif, i14s1..i14s7, adatin1..adatin4`.

`fw mix setup1` and `fw mix reset` are **canned routing presets** — relevant to
the open question of what ASFW should assert as default routing, though `setup1`
is Line-In monitoring, not stream→output.

`fw sync set` and `fw aux connect` are **writes**: they change clock source and
aux routing on a live device.

### `os` — KnOS introspection

Read-only: `os th`, `load`, `sem`, `q`, `blk`, `byte`, `timer`, `evt`, `flg`,
`map`, `mapt`, `mtype`, `mseg`, `mem`, `chk`, `mchk [nodump]`, `timestamp`.

**Do not run:** `os mlc` / `os free` (malloc/free — heap corruption),
`os suspend` / `os standby` / `os running` (power state), `os sleep <ms>` (stalls
the RTOS), `os wp` (software watchpoints), `os serprintf` (changes logging mode).

### `sc` — DM1001 coprocessor registers

```
sc isodrv | stat            avdrv state / statistics
sc tgen                     prints dm1001 TGEN register values
sc av                       prints dm1001 AV register values
sc frmr                     prints dm1001 framer register values
sc init                     init the dm1001                    (WRITE)
sc calib                    recalibrates the DCO               (WRITE)
sc ciphPutKey / deciphPutKey feed cipher/decipher with a key    (WRITE)
```

**`sc tgen` is the raw TGEN register dump** — `TgSytDelay`, `TgWsDelay`,
`TgLockThres`, `TgLockCnt`, `TgDiv`, `TgRefDiv`, `TgMisc`, `TgStatus`,
`TgIIA1`/`IIB0`/`IIB1`, `TgIntMask`, `TgIntSet`. **[measured]** field names from the
firmware's format strings; still unrun on hardware.

> An earlier revision of this document attributed those registers to
> `sys avstat full`. That was wrong — `sys avstat full` and `sys avstat all`
> produce identical output. The registers belong to this tree. The tell was in the
> formatting: `sc` uses fixed-width labels with no addresses, `sys avstat` uses
> `Name(ADDRESS) : value`.

### `avd` — a full ISO-link test harness

The largest tree, and the only one with per-command help pages. Read-only:

```
avd help | printcfg <iso-id> | printav | checkstate <delay> <state> | checkmdb <var>
```

`avd printcfg <iso-id>` "prints all information about the given link" — the per-link
`ISOConfig`, which per `avd cfg`'s own help includes `fdf`, `dbs`,
`dbcOffsetCode`, `queueElements` and the interface list. That is **the device's own
channel formation**, which is otherwise unobtainable because `PLUG_INFO` is unsafe
on this firmware.

`avd checkstate` polls the AV-driver state (`IDLE_STATE`, `AV_OFF_STATE`,
`RUNNING_STATE`, `SYNC_LOSS_STATE`, `TRY_SYNC_STATE`) — an assertion primitive for
automated tests.

**Write / mutating — every one of these perturbs a live link:**

```
avd flag | flagassert | cfg | pool | calcdb | setup | start | stop | close | reopen
avd verbose <level> | avd distort | avd avtoggle
avd dm1000av1|dm1000av2|dm1001av1|dm1001av2 <var> <value>
```

Three of them are notable as deliberate instruments rather than hazards:

- **`avd setup <iso-id> <src> <dst> [<thread>]`** with `src`/`dst` in
  `{AV, ARM, LINK1394}` and thread functions `forward`, `pulse`, **`sin1`
  ("fill sinus data into packets")**, `count`, `null`. The device can therefore
  **synthesise a sine directly into an isochronous stream** — a known-good RX test
  source with no analog path involved.
- **`avd distort`** — "distorts the in-lock detection of the PLL". A fault injector
  for sync-loss recovery testing.
- **`avd calcdb <iso-id>`** — "calculates dbs and dbcOffsetCode based on AV port
  settings and interfaces". The device computing its own DBS. Note it likely
  *writes* the result into `ISOConfig`, so prefer `printcfg` for reading.

`avd cfg`'s help documents an interface numbering (`1` = I2S stereo out, `9` =
S/PDIF out, `12` = S/PDIF in RCA, `13` = TOS link in, `14` = TOS link out) but
labels it **"Interface of Demo DSpeaker"** — the BridgeCo reference board, *not*
necessarily the 1814. Do not map the 1814's `avInterfacesUsed` through it without
corroboration.

### `bm` — the bus-master console, and the most interesting thing here

```
bm help | nodes | topo | irm
bm rd | wr | wrs | wrb | lock          transactions against another node
bm phywr | phyreg                      PHY packets and PHY register access
bm alloc | free                         IRM resource allocation
bm subscribe | unsubscribe              event queues
bm busreset | completeinit
```

**This makes the 1814 a scriptable second FireWire node.** `bm rd` / `bm wrb` /
`bm lock` let the *device* issue transactions at a target — including at us — which
is a way to exercise ASFW's async responder, CSR handling and lock support from the
other side of the bus, with no second controller. `bm topo` gives the device's own
topology map to cross-check ours, and `bm irm` its view of allocated bandwidth and
channels.

**Everything in this tree except `help`, `nodes`, `topo` and `irm` is mutating and
bus-visible.** `bm busreset`, `bm wr`/`wrs`/`wrb`, `bm phywr`, `bm alloc`/`free`
and `bm completeinit` all perturb global bus state — the exact class of thing the
wire-compat doctrine says not to do casually. Read-side use is a genuine test
instrument; write-side needs a deliberate plan.

---

## 5. Live Hardware Wire Analysis & Observed Phenomena

Live captures from an IEEE 1394 protocol analyzer during active Virtual UART sessions demonstrate the protocol on real silicon:

### 5.1 Successful Transaction Sequence
```text
059:5726  Bwrite fr ffc0 to ffc2.ffff.c802.1040, sz 10  --> "sys stat\r\n"
059:5777  Bwrite fr ffc0 to ffc2.ffff.c802.1000, sz 12  --> Opcode 0x09, operand=10
059:5781  WrResp from ffc2 to ffc0, rCode 0 (Success)
059:5996  Bwrite fr ffc0 to ffc2.ffff.c802.1000, sz 12  --> Opcode 0x08 (Read), max=1024
059:6000  WrResp from ffc2 to ffc0, rCode 0 (Success)
059:6055  Bread  fr ffc0 to ffc2.ffff.c802.9000, sz 12  --> Poll available bytes
059:6090  BRresp from ffc2 to ffc0, 12 bytes             --> Available = 128 bytes
059:6106  Bread  fr ffc0 to ffc2.ffff.c802.9040, sz 128 --> Read stdout buffer
059:6109  BRresp from ffc2 with payload                  --> Live RTOS stdout received!
```

### 5.2 The `resp_conflict_error` (`rCode 4`) Phenomenon
When multiple asynchronous Swift tasks (e.g. `async let` in ViewModel polling) submit block writes concurrently:
```text
059:5730  Bwrite fr ffc0 to ffc2.ffff.c802.1040, sz 14
059:5731  WrResp from ffc2 to ffc0, rCode 4 [resp_conflict_error]
```
* **Root Cause:** The DM1000 hardware mailbox is **half-duplex and single-occupancy**. It cannot queue overlapping 1394 async requests.
* **Resolution:** All Virtual UART requests must be funneled through a strict serial queue or Swift `actor` with single-flight locking.

### 5.3 The Endless PLL Calibration Loop Phenomenon
During initial bring-up, terminal sessions often show repeated lines:
```text
DM1001 digital pll calibration...
done.
 internal sync, pll: 1
```
* **Root Cause:** The DM1000/DM1001 contains a **DCO (Digitally Controlled Oscillator)**. When the device is unanchored (no incoming isochronous SYT stream), the clock watchdog task (`sub_200F8E30`) continuously times out, falls back to internal clock, re-calibrates the DCO, and logs to `/dev/uart1394`.
### 5.4 The Historical Boot Log FIFO & 128-Byte Chunk Protocol
When first opening the virtual shell, reading `0xFFFF_C802_9040` yields accumulated RTOS kernel startup traces:
```text
mod: scGpioModule
base address of CHIP_ID_DM1001 is 0x40000000

stack1394NoBusReset

mod: bldCommand
  bldCommandProcessor: config ROM returned max packet size of 512
  allocated 16 bytes
  allocated 128 bytes from memType 1
  allocated 64 bytes from memType 1
  allocated 144 bytes from memType 1
  allocated 296 bytes from memType 5

mod: modCMP
mod: Services1394
mod: AVCStack
mod: avDriver
mod: AvdCommands
mod: modScAvDriver
base address of CHIP_ID_DM1001 is 0x40000000
Warp loaded.
Warp started.
DM1001 digital pll calibration...
done.

mod: streamingDriver
mod: StreamingFrmWrk
mod: modBmCommand
mod: modOsCommand
mod: modDm1001Commands
```
* **Module architecture decoded** (from the boot log):
  * `mod: scGpioModule` & `CHIP_ID_DM1001 @ 0x40000000`: Confirms physical ARM memory-mapped base address for the DM1001 audio coprocessor.
  * `mod: modCMP` / `mod: Services1394` / `mod: AVCStack`: Connection Management Protocol and IEEE 1394 / AV/C stacks.
  * `Warp loaded / Warp started`: BridgeCo's proprietary "Warp" DSP mixing and sample routing engine.
  * `mod: streamingDriver` & `mod: StreamingFrmWrk`: The low-level DMA isochronous streaming pipeline.
  * `mod: modBmCommand`, `mod: modOsCommand`, `mod: modDm1001Commands`: The diagnostic shell command trees.
* **128-Byte Paging Behavior:** The BridgeCo UART controller transfers stdout in **128-byte pages** (`0x0080` in the `0x9000` response envelope operand).
* **FIFO Queueing:** Single 128-byte reads do not immediately return the output of a newly typed command; instead, they advance the FIFO by 128 bytes through historical boot logs.
* **Resolution (`drainStdoutFIFO` & UI Drain Logs Button):** A multi-chunk drain loop flushes the backlog so subsequent commands return their responses in real time.

### 5.5 Quadlet Alignment — the defect that made the shell look alive but deaf

Measured on hardware 2026-08-17. Block transactions against this mailbox must be
a whole number of quadlets, and **an unaligned request-buffer write silently
drops its tail.**

`"help\r\n"` is 6 bytes, so only `"help"` lands. The CR/LF is replaced by
whatever the *previous* command left at offsets 4–5 — after `"fw sync show"`
those bytes are `y`,`n`. The request envelope still declares 6, so the device
reads `help` + `yn`, echoes the printable characters, and — having never seen a
CR — **never executes the line**:

```text
$ help
helpynhelpynhelpynhelpyn…
```

The shell therefore appears to respond while only ever replaying its boot-log
backlog 128 bytes per keypress. Reproduced byte-exact by priming the buffer,
writing a 4-byte prefix, and declaring 6.

> **This is the mailbox's family-wide convention, not a shell quirk — and FFADO
> already demonstrates it.** Its firmware downloader writes payload blocks with
> `get1394Serivce()->write( …, AddrRegReqBuf, ( blockSize + 3 ) / 4, block )`
> (`bebob_dl_mgr.cpp:445`) — a quadlet count rounded **up** — while the
> `DownloadBlock` envelope carries `m_numBytes`, a true **byte** count
> (`bebob_dl_codes.h:225,233`). `WriteShellChars` wants exactly the same pairing.
> The rule was in the tree the whole time; the defect was not applying the
> download path's convention to the shell path.

**Rules that follow:**
* Pad the request payload to a quadlet; keep the envelope operand at the **true,
  unpadded** byte count so the device consumes exactly the command.
* Round read lengths up and trim: the stdout tail page is almost never aligned,
  and asking for the raw length fails the transaction outright.
* Terminate the drain on a **run of empty polls**, not on a short page. Output
  arrives in bursts, so the first short page is usually the *first* page of a
  reply, not the last.
* The shell terminates lines on **CRLF**. A bare LF is echoed but never runs.
* The device echoes the command and prints its own prompt — a UI that
  synthesises either splices duplicate text into the middle of the response.

### 5.6 Command names, corrected against the device

`sys avstat all` and `sys stat` are correct. `sys av` and `sys tgen` are
*different* handlers from `sys avstat`, and `sys av tgen` is rejected outright.
`fw sync show` is valid but reports only the sync source; **`fw show`** is the
command that carries audio state, sync source and sample rate together.

`sys avstat all` prints every latch unconditionally as
`    SetTgInLock       : 00000001`, so testing for the presence of a label says
nothing — only its value does. Use `sys avstat clr all`, soak, then re-read to
tell a live fault from a sticky power-on latch.

### 5.7 Dynamic Device Routing & Stale `DeviceInstanceId` After Bus Resets
When a bus reset or hotplug occurs:
```text
[UserClient] NOTICE [UserClient] AsyncBlockWrite: instance=1 has no routable current binding (missing, suspended, retired, or quarantined)
```
* **Root Cause:** The driver's `DeviceRegistry` invalidates all live generation routes upon bus reset (`InvalidateLiveMappingsForBusReset`). When the device re-enumerates, it receives a new `DeviceInstanceId` (e.g. `instance=2`).
* **Resolution:** The host layer must dynamically query `connector.getDiscoveredDevices()` to resolve the live `.ready` device ID and provide a UI target picker.

---

## 6. Current Implementation Status & Next Steps

### Implemented & Verified
- [x] 12-byte little-endian command builder with opcode safety filtering.
- [x] Async 1394 client transport in `ASFWDriver` (raw stdout crosses the UserClient).
- [x] Multi-chunk FIFO drain loop (`drainStdoutFIFO`) for complete command output retrieval.
- [x] Dynamic `DeviceInstanceId` resolution and UI Target Device Picker.
- [x] Column-aware telemetry parsers in the app (`ASFW/BeBoB/BeBoBShellTelemetry.swift`),
      tested against verbatim device captures (`ASFWTests/BeBoBShellFixtures.swift`).
- [x] SwiftUI monospace terminal and telemetry dashboard in `ASFW.app`.
- [x] 4 MCP control plane tools for automated agent investigation.

### Where the parsing lives, and why there is only one copy

Raw stdout crosses the UserClient; **the app parses it, the driver does not.**
A driver-side typed parser (`BeBoBStreamTelemetryParser`, `BeBoBTelemetryTypes`)
existed until 2026-08-18 with **zero callers** — it was a second implementation
of the same text format whose only observable effect was three unit tests that
passed against invented fixtures. It has been deleted. If the driver ever needs
`fw show` itself (see §3.3 of `1814.md` — choosing the channel formation table
from the device's actual digital format), write exactly what it needs against the
real captures, and do not resurrect a general parser it has no consumer for.

### Ongoing Refinements
1. **MCP tool exposure:** the `asfw_bebob_*` tools require an app rebuild and
   relaunch before they appear on the control plane. `tools/1814/bebob_shell.py`
   drives the mailbox with raw block transactions and needs neither.
2. **Automated Telemetry Assertion:** `asfw_bebob_get_streaming_stats` now returns
   `fireWireOutput` / `fireWireInput` alongside the raw text; assert on those, and
   never on the first column (see §6).
