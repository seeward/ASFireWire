# PreSonus FireStudio Project: capture provenance and validation limits

The experimental profile targets Config ROM vendor/model `0x000a92:0x00000b`
(`FIRESTUDIO_PROJECT`) and accepts 44.1/48 kHz with a 48 kHz default. Its captured
layout is one stream per direction, each with 10 PCM channels and one MIDI slot.
Other rates remain outside this profile's supported scope.

## Retained device report

[`dice-report.txt`](dice-report.txt) is the unchanged latest read-only app export,
captured **2026-09-08 at 08:00:03 UTC**, renamed from
`2026-09-08-dice-report-44100.txt`. SHA-256:
`35c7a79f8d6d4ccb850253b6269993cf85ccca19c5d8af34ca37fce7bc0f17d2`.

- FireStudio Project GUID `0x000A920402D07FAC`; TCD2210 / DICE Mini.
- MacBookPro18,3 / Apple M1 Pro; macOS 26.6.2 build 25G83.
- Apple Thunderbolt 3-to-2 and Thunderbolt-to-FireWire adapters, then FW800-to-FW400.
- ASFW 0.3.0 local build 8, based on `ac8a124` with the candidate changes.
- Internal clock locked at selected, nominal and measured 44,100 Hz.
- No owner, streaming disabled, both ISO channels disabled; 10 PCM + 1 MIDI in
  each direction. The earlier 48 kHz report shows the same stream geometry.

This export contains decoded registers, routing and a rounded mixer matrix;
it is not a raw Config ROM, coefficient or isochronous-packet capture. Its
embedded build timestamp is reproduced as reported. The report's TCAT product
number is derived from the GUID, so the archived Config ROM screenshot supplies
independent vendor/model evidence. Inactive rate tables do not establish support
for additional modes.

## Bounded hardware observations

The following results describe the **earlier labelled-AM824 candidates**, whose
PCM silence was `0x40000000`. They do not validate the raw-PCM candidate now in
this PR.

- September 7, build 5, 48 kHz: two short silent start/stop checks, stereo
  headphone playback and guitar inputs 1/2 passed. The contributor also reported
  GarageBand recording/playback; no take or project metadata was exported.
- September 8, build 8: six three-second silent runs passed across 44.1/48 kHz,
  including idle rate changes. Two 24-second tone runs completed with transport
  progress and successful shutdown in the reviewed logs. The contributor heard
  test tones and reported digital clock lock on a Roland VM-3100 connected to
  the coaxial S/PDIF output at 44.1 kHz.
- Build 8's running driver SHA-256 was
  `591fe51b261cd7d8cea007c1be78856fad1057e2d0bcd33c8d901a65d8f79f87`.
  Local build counters and generated version metadata are not included in this
  contribution; a rebuilt PR artifact has not had a separate hardware run.

The repository owner reports that the original PreSonus vendor KEXT transmits
raw sign-extended 24-in-32 PCM with zero-based silence (`0x00000000`). This is
attributed vendor-binary evidence, not an independently captured wire format.
The raw-PCM change and standard zero-filled silence still require hardware
verification; the earlier listening results do not establish their success.

The saved mixer sends the same mix to both S/PDIF channels, including playback
1/2. No router, mixer coefficient or flash changes were made. Independent digital
left/right routing, digital capture, bit-perfect transfer, input waveform quality,
MIDI, inputs 3–8 individually, physical Main/line jacks, other rates, sleep/wake,
long-run stability and calibrated latency remain unverified. The capture-derived
DBS of 11 was not observed on the wire. Mixer controls and headphone-mix management
remain outside this contribution.

## Current candidate software checks

The revised PR passed 247 targeted host tests and all 38 tests in the TCAT
executable under ThreadSanitizer without diagnostics. A separate raw-PCM build 9
Release app/driver passed signature, entitlement and arm64e checks. It remains
uninstalled and hardware-unverified; the known build 8 installation is unchanged.

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
