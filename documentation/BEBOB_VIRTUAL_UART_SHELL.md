# BridgeCo BeBoB Virtual UART & Interactive Diagnostic Shell

## 1. Overview & Architectural Significance

The **BridgeCo DM1000 / DM1001 / DM1500** audio chipset family (used across M-Audio FireWire 1814, ProjectMix I/O, 410, Solo, Terratec Phase 88, PreSonus FireBox/FireStudio, and Focusrite Saffire) runs an embedded real-time operating system (**Express Logic ThreadX RTOS**).

During production and factory bring-up, BridgeCo implemented an in-band diagnostic shell over IEEE 1394 asynchronous block transactions, mapped to `/dev/uart1394`. This interface exposes low-level silicon status, hardware FIFO depths, DCO/PLL calibration states, isochronous framer error latches, and ThreadX RTOS task statistics.

In ASFireWire (ASFW), this subsystem provides **silicon-level observability** directly from the hardware, resolving the "black box" nature of FireWire audio stream bring-up.

```
                           ┌──────────────────────────────────────────────┐
                           │      Express Logic ThreadX RTOS Core         │
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
* **Role:** The ThreadX RTOS mailbox polling task.
* **Mechanism:** Listens for 1394 block writes at `0xC802_1000`. Upon receiving Opcode `0x09`, it reads the byte count from operand quadlet 2, copies the characters from `0xC802_1040` into the shell line buffer, and wakes `Shell_DispatchCommand`.

### 3.2 `Shell_DispatchCommand` (`0x200CBE10`)
* **Role:** Master command table parser.
* **Available Command Trees:**
  * `sys`: System, streaming, memory, LLC, and hardware diagnostics.
  * `fw`: 1394 bus, sync manager, and isochronous connection inspection.
  * `os`: ThreadX RTOS task list, stack usage, semaphores, and byte pools.
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
* **ThreadX RTOS Architecture Decoded:**
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
- [x] Strongly-typed telemetry models (`BeBoBStreamingStats`, `BeBoBAvStat`, `BeBoBSyncState`).
- [x] 12-byte little-endian command builder with opcode safety filtering.
- [x] Zero-allocation string telemetry parsers.
- [x] Async 1394 client transport in `ASFWDriver`.
- [x] Swift driver connector bridge & ViewModels.
- [x] Multi-chunk FIFO drain loop (`drainStdoutFIFO`) for complete command output retrieval.
- [x] Dynamic `DeviceInstanceId` resolution and UI Target Device Picker.
- [x] SwiftUI monospace terminal and telemetry dashboard in `ASFW.app`.
- [x] 4 MCP control plane tools for automated agent investigation.
- [x] 7 GoogleTest unit tests covering encoding, safety, and parsing.

### Ongoing Refinements
1. **Telemetry parsers vs. real output:** `BeBoBStreamTelemetryParser` parses a
   `rxPackets:  12450` shape the device never emits. Real `sys stat` output is a
   **three-column table with no colons**, one column per iso channel. The unit
   tests currently pass against an invented fixture.
2. **MCP tool exposure:** the `asfw_bebob_*` tools require an app rebuild and
   relaunch before they appear on the control plane. `tools/1814/bebob_shell.py`
   drives the mailbox with raw block transactions and needs neither.
3. **Automated Telemetry Assertion:** Use `asfw_bebob_get_streaming_stats` in
   automated tests to verify `sytDiffErr == 0` during active playback.
