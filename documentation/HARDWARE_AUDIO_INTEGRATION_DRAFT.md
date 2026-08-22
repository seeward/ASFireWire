# Draft: hardware audio integration and console-tuning guide

**Status:** working draft, 2026-08-22.  This is an engineering guide and a
record of lessons learned from real hardware work.  It is intentionally not a
finished design document or a claim that the M-Audio 1814/ProjectMix I/O and
Apogee Duet implementations are complete.

The architectural baseline remains [AUAA](../ADKVirtualAudioLab/AUAA.md).  Its
central rule is right: model the hardware that is actually present, then project
that one committed reality to the stream engine, AudioDriverKit, and the UI.
This document adds the field practice that AUAA did not yet capture: how to
establish that reality, how to avoid inventing a convenient one, and how to tune
the resulting control console.

## 1. The operating model

Treat a device integration as five deliberately separate jobs:

```text
hardware / wire capture
        ↓
family or vendor protocol + SERDE
        ↓
device profile and resolved stream plan
        ↓
semantic topology + runtime state
        ↓
AudioDriverKit and a hardware-console projection
```

The layers answer different questions.

| Layer | It owns | It must not own |
| --- | --- | --- |
| Wire/transport | packet transport and stream framing | vendor control meaning or UI labels |
| Protocol/SERDE | bytes, endianness, validation, command framing | `Mic gain`, `DAW`, or a SwiftUI control |
| Device profile | proven quirks, supported modes, control bindings, stream positions | generic transport policy |
| Semantic topology | ports, routes, crosspoints, parameters, meters, presentation hints | FCP opcodes, vendor IDs, register offsets |
| App projection | render and interact with the semantic model | infer hardware behavior from its layout |

Do not collapse those layers because a small device appears simple.  The Duet
has two analogue inputs and a stereo output, but still has vendor-dependent
AV/C, an async meter/knob path, a 4x2 cue mixer, configuration-dependent stream
cadence, and output/headphone mute policy.  Conversely, a generic BeBoB unit can
need a device-specific stream-position map while its controls remain generic.

## 2. Sources of truth and the order to use them

Use the strongest available evidence first.  A lower source may explain an
observation, but it must not silently override a stronger one.

1. **The connected device and a controlled wire capture.**  This is the final
   answer for channel order, packet cadence, transaction direction, and what a
   write actually changes.  Record test signal, sample rate, stream direction,
   channel/slot, and the device's physical result.  FireBug's displayed word
   order is a tool presentation, not proof of payload endianness; establish the
   order from the transaction type and the protocol parser.
2. **The original vendor daemon/driver and control application.**  Use this to
   find command catalogues, async-state readers, ranges, and poll cadence.  A
   decompiled opcode is evidence that it exists, not permission to expose a
   write: confirm direction, response, and hardware semantics first.
3. **Local reference stacks.**  Before implementing wire-visible behavior,
   inspect the applicable source under `references/` if it is present:
   `linux-sound-firewire-stack/` for audio-family behavior, the matching
   userspace control service or FFADO for vendor control, and the Apple source
   for legacy FireWire policy.  They are behavioral references only.  Do not
   copy their code into ASFW; preserve their licenses and write a fresh
   implementation.
4. **AUAA and existing ASFW integrations.**  They are the architectural and
   implementation vocabulary, not evidence that a newly attached device shares
   their behavior.
5. **Specifications and secondary documentation.**  Use these only when the
   exact rule is known and applicable.  Do not manufacture a section reference
   or fill gaps with a plausible bit layout.

If no reference or capture exists, say that the behavior is unverified.  Add a
diagnostic/inspection path or ask for a capture instead of making the normal
driver path speculative.

## 3. Before writing code: make a hardware evidence sheet

Create a short device note before the first feature patch.  It should answer:

- **Identity:** vendor/model, config-ROM identifiers, firmware, transport
  family, and which reference source applies.
- **Stream geometry:** capture/playback stream count, PCM format, supported
  rates, block/cycle cadence at each rate, and rate-dependent topology.
- **Wire positions:** for every CoreAudio channel, its semantic port and actual
  on-wire channel/slot.  Never assume jack order.  BeBoB/BridgeCo may describe
  positions separately; AM824 may be planar while the panel is interleaved.
- **Physical routing:** a signal-in/signal-out table verified one channel at a
  time.  Include monitor, headphones, digital I/O, ADAT/S/PDIF mode and any
  fixed links.
- **Control inventory:** name, native command/readback, range/unit, state
  origin (observed/cached/derived), safe write evidence, and whether it is
  available in every configuration.
- **Telemetry:** meter source, raw unit, channels, request/notification
  mechanism, actual cadence, and whether it is optional.
- **Known unknowns:** opcodes seen but not decoded, unsafe commands, UI
  behavior not yet tied to a wire action, and hardware tests still required.

Keep the sheet beside the device protocol or in `documentation/` and cite the
capture/commit that established a non-obvious fact.  That is much more useful
than a giant, unqualified list of guessed controls.

## 4. Stream mapping: validate the wire before polishing controls

Channel mapping errors often look like a mixer or UI bug.  Establish stream
positions before making UX conclusions.

1. Search the relevant Linux family implementation for the device or its
   protocol family.  For BeBoB, inspect stream-format discovery and BridgeCo
   channel-position handling rather than assuming that the advertised number of
   channels is jack order.
2. Compare the result with ASFW's discovered/parsed capabilities and make the
   mapping an explicit profile/resolved-stream-plan fact.
3. Send a distinctive signal on exactly one host channel or inject one physical
   input at a time.  Capture enough packets to locate the active AM824 slot.
4. Repeat in both directions and at every supported rate.  Packet cadence and
   frame packing change at 44.1 kHz and multiplied rates; a correct 48 kHz map
   does not validate the 44.1 kHz path.
5. Add a host test for the parsed position/map and a hardware test note for the
   physical result.

The Phase 88 debugging is the warning case: a channel visible in slots 0/1 did
not mean that the corresponding panel outputs were interleaved as expected.
The generic BeBoB fixes made the parsed BridgeCo map authoritative for both
directions:

- `b18990e8` — capture channel positions;
- `eca50e5c` — playback channel maps;
- `fdee3db1` and `41e46404` — BridgeCo replies and channel positions.

Do not reduce those to an 1814/PMIO quirk.  They are a family-level correction;
individual devices still need a physical signal-map test.

## 5. Vendor controls: catalogue first, bind second

Keep protocol representation in the device/family layer.  For a vendor device,
put the command numbers and native constants in a narrow vendor header, then
give the codec/SERDE ownership of serialization, byte order, request length,
and response validation.  `ApogeeDuetVendorCommands.hpp` is the current Duet
example.

For each command, record all of:

| Fact | Why it matters |
| --- | --- |
| Request type/direction and response form | a read-like request may not be a safe set operation |
| Native byte order and field range | FireBug display order can be misleading |
| Readback/notification source | defines observed, cached, or derived state |
| Atomicity and companion writes | one semantic action may require several native operations |
| Configuration applicability | prevents controls surviving a topology/mode change |
| Hardware proof | keeps unknown commands diagnostic-only |

Only map a control into the common semantic vocabulary when that vocabulary
describes a fact shared by real devices.  Otherwise keep it as a vendor extension
or a device-specific UI control.  The semantic model must never learn an AV/C
opcode merely to avoid creating a small protocol object.

An opcode catalogue can be broader than the enabled feature set.  For example,
an unknown or insufficiently tested Duet command can be named in the vendor
header and intentionally not be emitted by normal control serialization.  That
is a safety boundary, not missing work.

## 6. Runtime state, meters, knobs, and polling

AUAA's distinction between **structure** and **runtime state**, and between
**observed**, **cached**, and **derived** state, is mandatory in real hardware.
The UI must not represent a cached host write as if it had been confirmed by the
device.

- Meter objects are scalar telemetry points.  Stereo is two meters; the rack
  decides to draw them as a stereo pair.
- Meters are read-only, high-cadence data.  They use their own polling or
  notification path and must be opt-in.  Enabling them must start the driver
  telemetry path; disabling or leaving the console must stop it.
- Front-panel encoders often travel over the same async state read as other
  telemetry.  Do not wait for a slow configuration snapshot to refresh a knob.
- Measure vendor cadence from a trace when possible.  The Duet daemon's observed
  polling is roughly 33 Hz, so the current driver control/telemetry cache uses a
  30 ms cadence.  A 10 Hz guess would visibly lag a knob; an unnecessarily
  faster loop increases FireWire traffic without improving device truth.
- The driver owns hardware polling and the cached snapshot.  The SwiftUI model
  owns a lightweight periodic read of that snapshot and only publishes a new
  value when its sequence changes.  SwiftUI must never issue a blocking hardware
  poll on its render path.
- Meter rendering is a performance feature: retain only the latest value,
  avoid rebuilding the console for an unchanged telemetry sequence, and keep
  animation/formatting work out of the high-rate path.

Meter quality is not cosmetic.  Slow or lurching meters are evidence that the
ownership, cadence, or main-actor publication boundary is wrong.

## 7. Configuration is a single transaction, not a UI selection

A rate/mode request is not committed merely because AudioDriverKit accepted a
configuration request.  It is committed only once all of these agree:

```text
hardware rate/mode
    = resolved stream geometry and CIP cadence
    = published semantic topology/revision
    = AudioDriverKit format/rate projection
    = app configuration snapshot
```

The Duet's temporary 44.1 → 48 kHz UI snap illustrated the distinction: the
app refreshed an old committed snapshot immediately after an accepted request.
The corrected UI maintains a pending value until a later authoritative snapshot
confirms the new rate, or reports the actual committed value after timeout.  It
does not pretend that acceptance was hardware completion.

For every supported rate and mode, verify:

- capability advertisement and profile resolution;
- device command order and readback;
- AM824/CIP packet frame cadence, especially 44.1 kHz and rate multipliers;
- stream channel count and channel map;
- semantic topology replacement and stale-state invalidation;
- AudioDriverKit format publication; and
- UI labels, units, and disabled controls.

Configuration-dependent control state belongs in the profile/resolver; it must
not be encoded as scattered UI `if device == ...` checks.  The 1814/ProjectMix
configuration-aware state and capture mapping are the current pattern
(`9377e288`).

## 8. Semantic topology and presentation hints

The graph is structural truth: endpoints, ports, fixed links, router bundles,
mixer crosspoints, parameters, and meters.  Crosspoints say that a contribution
exists; they do not themselves dictate a fader or a label.

Presentation hints are the intentionally narrow bridge to a useful console:

- group related crosspoints (`Input monitor`, `Host playback`);
- identify the primary fader versus a routing/send control;
- retain semantic source/destination identity, channel count, units, ranges,
  state origin, and meter relation;
- make a device-specific projection only when hardware semantics require it.

The Duet should therefore show one `Input Monitor` stereo source and one `DAW
Playback` stereo source, each with its L/R contributions.  It must not create
two duplicated strips such as `DAW L` and `DAW R`, each containing another L/R
pair.  A console layout is a projection of a matrix, not a substitute for
understanding that matrix.

When a device exposes a relationship that has no common semantic analogue, add
the smallest well-defined common concept only after evidence from more than one
device or keep the feature as a vendor extension.  The Duet input stereo-link
bit is a real vendor control; it does not justify making arbitrary link buttons
appear on every generic mixer strip.

## 9. Console UI grammar

The M-Audio console is the visual reference for ASFW's hardware-console style:
compact strips, stable color families, clear vertical fader/meter pairing,
legible scale values, and horizontal overflow rather than a second unrelated
row of faders or a maze of tabs.  Reuse its grammar, not its device topology.

Shared SwiftUI primitives now carry that grammar:

- `AudioConsoleStripShell`, `AudioConsoleRackBank`, and
  `AudioConsoleRackDivider` establish rack grouping;
- `AudioConsoleVerticalFader`, `AudioConsoleMeter`, and scale helpers establish
  consistent control/meter geometry;
- `AudioConsoleStripSlots` and `AudioConsoleStripToggle` preserve compact strip
  composition;
- device views compose those primitives into an input strip, stereo mixer
  source, or output strip rather than reimplementing a generic settings form.

Rules for a new hardware console:

1. Start from a strip inventory derived from topology and a hand-drawn signal
   flow, not from the vendor application's tabs.
2. One physical/signal concept gets one strip.  Pair stereo meters and controls
   where the hardware exposes a stereo concept.
3. Put the meter beside the fader it measures; label its scale in a readable
   unit.  Never show raw native values (`0…75`) as dB without a proven mapping.
4. Show only usable controls.  A fixed line-level input does not need a live
   gain fader; a display-only output condition does not need a fake toggle.
5. Put exceptional state near its owner: input source/phantom/polarity on the
   input strip; monitor source, nominal level, global mute and HP policy on the
   output strip; cue sends on the source strip.
6. Use horizontal scrolling for a genuinely wide hardware rack.  Do not split a
   single mixer's source strips into arbitrary rows just to fit the window.
7. Treat colors and labels as semantic, not decoration: input, host playback,
   output, and exceptional routing should remain recognisable across devices.

The UI is a diagnostics surface too.  It should make meter enablement, pending
configuration, state origin when relevant, and unavailable hardware features
clear without becoming a generic debug parameter dump.

## 10. A repeatable implementation sequence

1. **Reconnaissance.**  Locate the family reference and collect the evidence
   sheet.  Add a raw explorer/trace request only if the existing inspection path
   cannot answer the question.
2. **Stream proof.**  Implement and test the stream map/cadence before controls.
   Validate physical capture and playback separately.
3. **Protocol boundary.**  Add protocol objects, SERDE, and a vendor command
   header.  Unit-test byte layouts, endian conversions, invalid lengths, and
   range rejection.
4. **Safe state surface.**  Implement only confirmed reads and writes, with
   explicit observed/cached/derived provenance.  Keep risky commands disabled.
5. **Profile/resolution.**  Bind capabilities, configuration mode, stream plan,
   topology, and control availability in one device profile.
6. **Semantic topology.**  Build structural graph and runtime-state projection.
   Add presentation hints only where the graph alone does not tell the console
   what to plot.
7. **ADK/app projection.**  Add CoreAudio controls and the console against the
   semantic contract, then create device-specific composition only as needed.
8. **Telemetry.**  Add opt-in meters/async hardware state at measured cadence;
   profile app publication and rendering under live traffic.
9. **Hardware matrix.**  Test every supported rate/mode, each physical input and
   output, route, mute policy, knob state, meter source, bus reset/reconnect,
   and a disabled-meter idle period.
10. **Document and commit.**  Record evidence and known gaps in a device note;
    split commits by protocol, semantic/app state, and presentation so a wire
    change is reviewable without a UI rewrite.

## 11. Current case studies and known gaps

### M-Audio FireWire 1814 / ProjectMix I/O

Relevant history:

- `40013b8d` documents MIDI slot multiplexing and latency;
- `e1c46b36` documents capture-map and analogue input skew;
- `9377e288` makes control state and capture maps configuration-aware.

The implementation proves the value of a rich device profile: unusual capture
ordering, special-firmware variants, optical mode, routing, and control state
cannot safely be reconstructed by generic UI logic.  Its console establishes
the shared hardware-rack style, but it remains a tuning reference rather than a
final universal component catalogue.  Every real control needs a review for
unit/range readability, ownership, and topology applicability.

### Apogee Duet FireWire

Relevant history:

- `604dd790`, `54404fcc`, `515dc86e`, and `c4b15c93` establish semantic
  topology and binding;
- `125e4396` adds the control/telemetry path, vendor command catalogue,
  optional metering, async knob refresh, sample-rate support, and mute-follow
  controls;
- `49efb6a5` makes the app state/pending configuration follow authoritative
  snapshots;
- `fad89171` extracts shared console primitives and reuses the M-Audio visual
  language for the Duet.

The Duet has working core behavior but is not declared done.  Remaining work is
hardware tuning: validate every control against the physical unit, confirm the
44.1 kHz stream path and cadence under audio, establish the exact headphone/main
mute combinations, verify stereo link behavior, and tune meter scaling/visual
response from real levels.  A vendor-app screenshot is a useful clue, not a
semantic specification or a UX template.

## 12. AUAA drift to reconcile deliberately

AUAA is still the design baseline, but real integration has exposed details
that should be reconciled rather than hidden:

- runtime topology now carries presentation-oriented crosspoint grouping and
  fader/routing hints so a console can make a faithful projection;
- mute-follow policy and comparable device facts have been added where hardware
  evidence made their semantic role clear;
- optional telemetry has concrete lifecycle and cadence requirements beyond the
  abstract meter model;
- the app needs an explicit pending configuration state, because ADK request
  acceptance is not hardware commit;
- vendor command catalogues are valuable even when selected commands remain
  intentionally disabled pending evidence.

These are candidates for an AUAA revision after they survive another device
family.  Do not backfill the architecture document as if all current details
were settled; keep the evidence, distinction between draft and contract, and
known shortcomings visible.

## 13. Definition of ready for a hardware support claim

Do not call a device support implementation complete until it has:

- an evidence sheet and explicit stream bindings;
- parser/SERDE tests and semantic topology tests;
- a clean build plus Swift projection tests;
- verified capture and playback maps at every claimed rate/mode;
- confirmed controls with honest state provenance;
- optional telemetry that is responsive when enabled and quiet when disabled;
- a console whose strips, meters, ranges, and routing are traceable to the
  topology rather than a screenshot; and
- a documented residual-risk list for untested operations.

Until then, the correct status is **working hardware integration under tuning**.
