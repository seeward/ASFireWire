# PreSonus FireStudio Project: capture provenance and validation limits

The experimental profile targets Config ROM vendor/model `0x000a92:0x00000b`
(`FIRESTUDIO_PROJECT`) and accepts 44.1/48 kHz with a 48 kHz default. Its captured
layout is one stream per direction, each with 10 PCM channels and one MIDI slot.
Other rates remain outside this profile's supported scope.

## Retained device report

[`dice-report.txt`](dice-report.txt) is the unchanged latest read-only app export,
captured **2026-09-08 at 09:13:36 UTC**, renamed from
`ASFW_DICE_Report_44100_before.txt`. SHA-256:
`82e769836bff9fd9aa71c5999515fbfa78f455cebc80686a7d3b310fbd8ec00c`.

- FireStudio Project GUID `0x000A920402D07FAC`; TCD2210 / DICE Mini.
- MacBookPro18,3 / Apple M1 Pro; macOS 26.6.2 build 25G83.
- Apple Thunderbolt 3-to-2 and Thunderbolt-to-FireWire adapters, then FW800-to-FW400.
- ASFW 0.3.0 local build 9, built on `458673e` with the then-uncommitted changes
  now committed through `6b054aa`. The report retains its original source label.
- Internal clock locked at selected, nominal and measured 44,100 Hz.
- No owner, streaming disabled, both ISO channels disabled; 10 PCM + 1 MIDI in
  each direction. The earlier 48 kHz report shows the same stream geometry.

This export contains decoded registers, routing and a rounded mixer matrix;
it is not a raw Config ROM, coefficient or isochronous-packet capture. Its
embedded build timestamp is reproduced as reported. The report's TCAT product
number is derived from the GUID, so the archived Config ROM screenshot supplies
independent vendor/model evidence. Inactive rate tables do not establish support
for additional modes.

## Current raw-PCM hardware validation

On September 8, **build 9** used raw sign-extended 24-in-32 playback PCM and
standard zero-filled silence (`0x00000000`). The installed running driver was
verified against SHA-256
`cccfb901044ab6fc31e951fc79914c6e9111a78abeb6223f2e1ab9fb2fa83443`.
Local build counters and generated version metadata are excluded from this PR;
this hash identifies the tested binary, not every rebuild of the same source.

- Five three-second silent runs passed at 48, 44.1, 44.1, 48 and 44.1 kHz, in
  that order, including idle rate changes. Client and driver checks showed
  contiguous output timestamps, successful start/stop and zero TX underruns.
- The contributor confirmed digital clock lock on Roland VM-3100 DIGITAL IN A
  (DIN-A), connected to the FireStudio's coaxial S/PDIF OUT at 44.1 kHz.
- A 24-second sequence alternated quiet 440/880 Hz tones at -36 dBFS with silent
  gaps. It completed all 1,058,400 sequence frames over 2,069 callbacks with
  contiguous output timestamps, 193,158 transmitted packets, 31,516 transmit
  interrupts, zero TX underruns and successful shutdown. No driver fault was
  found in the reviewed run log.
- Asked whether both tones were clear and the gaps quiet, with no clicks, buzz,
  distortion or dropouts, the contributor replied: **“Both clear; gaps quiet”.**

The repository owner reports that the original PreSonus vendor KEXT uses this
raw PCM format and zero-based silence. That remains attributed vendor-binary
evidence; the build 9 run establishes bounded audible playback and quiet gaps
on this unit, not an independent capture of the vendor's wire format.

## Historical labelled-AM824 results

The earlier builds below used labelled AM824 with PCM silence `0x40000000`.
Their input and analogue-output results have not been repeated with build 9.

- September 7, build 5, 48 kHz: two short silent start/stop checks, stereo
  headphone playback and guitar inputs 1/2 passed. The contributor also reported
  GarageBand recording/playback; no take or project metadata was exported.
- September 8, build 8: six three-second silent runs passed across 44.1/48 kHz,
  including idle rate changes. Two 24-second tone runs completed with transport
  progress and successful shutdown; the contributor reported clock lock and
  audible S/PDIF test tones through the Roland at 44.1 kHz.

## Remaining validation limits

The saved mixer sends the same mix to both S/PDIF channels, including playback
1/2. No router, mixer coefficient or flash changes were made. Independent digital
left/right routing, digital capture, bit-perfect transfer, input waveform quality,
MIDI, inputs 3–8 individually, physical Main/line jacks, other rates, sleep/wake,
long-run stability and calibrated latency remain unverified. The capture-derived
DBS of 11 was not observed on the wire. Mixer controls and headphone-mix management
remain outside this contribution.

## Software checks

The revised PR passed 247 targeted host tests and all 38 tests in the TCAT
executable under ThreadSanitizer without diagnostics. The installed raw-PCM
build 9 Release app/driver passed signature, entitlement and arm64e checks.
Host tests use DriverKit stubs and do not establish bus-reset recovery on hardware
or coverage of other DICE devices.

## Archived evidence

The initial report, screenshot, raw meter/probe logs, lifecycle excerpts and
longer summaries remain accessible at immutable commit
[`458673e2`](https://github.com/mrmidi/ASFireWire/tree/458673e250d3cea67470a9b350e15eaf98d15c8a/captures/presonus-firestudio-project).
In particular:

- [Initial September 7 report](https://github.com/mrmidi/ASFireWire/blob/458673e250d3cea67470a9b350e15eaf98d15c8a/captures/presonus-firestudio-project/2026-09-07-dice-report.txt),
  SHA-256 `2b0c0bd55b6bd55e322bf5ba6cf5b5ebc7262d1e3ee38f8394928631d2beba3e`.
- [Config ROM identity screenshot](https://github.com/mrmidi/ASFireWire/blob/458673e250d3cea67470a9b350e15eaf98d15c8a/captures/presonus-firestudio-project/2026-09-07-device-properties.png).
- [September 7 results and references](https://github.com/mrmidi/ASFireWire/blob/458673e250d3cea67470a9b350e15eaf98d15c8a/captures/presonus-firestudio-project/README.md#september-7-hardware-validation-build-5).
- [September 8 validation and earlier fault history](https://github.com/mrmidi/ASFireWire/blob/458673e250d3cea67470a9b350e15eaf98d15c8a/captures/presonus-firestudio-project/2026-09-08-validation.md).

These archived notes describe their historical candidates, including superseded
publication/cache and labelled-silence behavior. This directory retains only the
latest device report and this scope summary.
