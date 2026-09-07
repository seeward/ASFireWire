# PreSonus FireStudio Project: experimental 48 kHz support

This profile enables the exact FireStudio Project model `0x000a92:0x00000b`
using stream geometry captured from one real unit. Short Core Audio tests confirmed
guitar inputs 1 and 2 and stereo headphone playback. The owner subsequently
confirmed recording and playback in GarageBand 10.4.14. Other inputs, physical
output jacks, digital/MIDI operation and sustained stability remain unvalidated.

**The unit must already report 48 kHz when discovered.** This initial profile
advertises only 48 kHz and refuses publication if the observed rate or stream
geometry differs. It does not automatically retune a unit found at another rate.
The captured wire layout is 10 PCM channels plus one MIDI slot per direction;
that layout is preserved even when an application uses only inputs/outputs 1–2.
Internal clock was the tested setting; the profile does not enforce a clock source.

## Capture provenance

- Initial read-only capture: 2026-09-07 at 11:39:39 UTC.
- MacBookPro18,3, Apple M1 Pro, macOS 26.6.2 build 25G83.
- Initial ASFW app/driver: 0.3.0 build 4, source
  `ac8a124a683d2f8201cd14ee0d2de8265e4834f0`.
- Connection: Apple Thunderbolt 3-to-2 adapter, Apple Thunderbolt-to-FireWire
  adapter and FW800-to-FW400 cable. macOS reported controller `pci11c1,5901`.
- [Initial DICE report](2026-09-07-dice-report.txt): unchanged app export,
  SHA-256 `2b0c0bd55b6bd55e322bf5ba6cf5b5ebc7262d1e3ee38f8394928631d2beba3e`.
- [Device Properties screenshot](2026-09-07-device-properties.png): independent
  Config ROM vendor/model evidence. The DICE report's TCAT product number is
  derived from GUID bits, so it is not independent identity evidence.

Network MCP remained disabled. The initial capture issued no owner, clock,
stream, router or flash writes; the driver had initialized the controller for
discovery. The reports contain decoded registers, not raw Config ROM bytes,
raw mixer coefficient quadlets or isochronous packets. Driver build timestamps
are reproduced as reported, not treated as wall-clock compile times.

## Observed configuration

| Field | Captured value |
| --- | --- |
| Vendor/model | `0x000a92 / 0x00000b` |
| GUID | `0x000A920402D07FAC` |
| Model string | `FIRESTUDIO_PROJECT` |
| ASIC | TCD2210 / DICE Mini |
| DICE protocol version | `0x01000400` (1.0.4.0); vendor firmware build not established |
| Current clock | Internal; selected, nominal and measured 48,000 Hz; locked |
| Owner / streaming | No owner (`0xffff000000000000`); GLOBAL_ENABLE=0 |
| Device TX → host capture | 1 stream, 10 PCM channels, 1 MIDI port, ISO=-1, S400 |
| Device RX ← host playback | 1 stream, 10 PCM channels, 1 MIDI port, ISO=-1, SEQ_START=0 |
| Descriptor stride | 70 quadlets / 280 bytes in each direction |
| Capture channel labels | Mic 1–8, SPDIF L, SPDIF R |
| Playback channel labels | daw rt.1 through daw rt.10 |
| Clock capabilities | `0x1102001f`: 32/44.1/48/88.2/96 kHz; AES2, ARX1, Internal bits |
| Clock label caveat | AES2 is labelled SPDIF; advertised ARX1 is labelled Unused |
| EAP stream limits | 1 TX and 1 RX stream |
| EAP mixer | 18 inputs, 16 outputs; exposed/writable/storable |
| EAP router | Exposed/writable/storable, maximum 128 entries |

General section offsets are device-reported: global `+0x28`, TX `+0x190`, RX
`+0x3c8`, ext-sync `+0x830` from `0xffffe0000000`. These are not universal DICE
constants. TX/RX sections reserve space for two/four descriptors, but their
NUMBER registers report **one** stream. Allocated capacity is not stream count.

Stored low- and middle-rate tables both report 10 PCM + 1 MIDI each way, with
identical 82-entry route tables. Only 48 kHz was active during capture. The
high-rate table contains 8-channel AES defaults, but 176.4/192 kHz are absent
from clock capabilities; inactive table contents do not establish supported
modes. Standalone AES1/32 kHz settings do not override the active global clock.

## Framing and discovery safeguards

The captured PCM/MIDI counts imply DBS 11 under standard DICE AM824 framing.
**DBS was derived from descriptors, not observed in a packet capture.** The
profile uses blocking AM824, eight frames per DATA packet at 48 kHz, FMT=0x10,
DATA FDF=0x02, and header-only NO-DATA with FDF=0xff/SYT=0xffff. DATA contains
360 bytes including CIP. PCM silence is `0x40000000` and empty MIDI is
`0x80000000`, serialized big-endian.

These choices follow the Project's generic Linux/FFADO streaming paths and
produced working audio in the bounded tests below. StudioLive raw-PCM and
NO-DATA FDF-preservation quirks are not applied to this device. Default buffer
and latency settings remain generic; physical round-trip latency was not measured.

The exact runtime constraint covers rate, stream counts, PCM channels and MIDI
ports/slots; ISO channel allocation is deliberately excluded. Discovery must
succeed with usable caps before audio publication. A later geometry mismatch
rejects preparation and rolls back ownership before completing the request.
Truncated declared stream descriptors must not be interpreted as a smaller
valid layout. Encoding-aware AM824 defaults keep unwritten/pre-roll PCM slots
labelled as silence while preserving raw-PCM behavior.

The failed-discovery publication guard and descriptor parsing checks apply to
other DICE models too. A failed capability read leaves the endpoint unpublished;
there is no new retry loop in that callback. Another device-record update or
reconnect is needed to retry. Other DICE models were covered by host tests, but
were not tested on hardware for this change.

## Existing routing

The captured Project endpoint map agrees with FFADO and the ALSA Rust protocol.
With zero-based register indices:

- Capture 0–7 receives Ins0 0–7; capture 8–9 receives AES 2–3.
- Analogue output 0–1 receives mixer output 0–1.
- Analogue output 2–7 receives playback 2–7 directly.
- S/PDIF output receives mixer output 8–9.
- Playback 0–1 enters mixer input columns 10–11.

The manual describes Main as sharing the line 1–2 source with its own level
control. The captured matrix sends DAW 1 to the left mix at -9.9 dB and DAW 2
to the right at -10.2 dB, with opposite stereo crosspoints muted. Other inputs
also feed this mix. The profile does not change routing, mixer coefficients
or flash settings.

Initial saturation bits `0x3ff` and full-scale routed mixer peak codes are one
snapshot, not proof of continuous clipping or a driver fault. Meter hold/clear
behavior was not established. Quiet headphone playback was subsequently heard
without distortion.

## Hardware validation

Tests used the local 0.3.0 build 5 candidate containing these source changes.
Its running driver executable was verified against the candidate SHA-256
`4bae5ce16eb6ae43a52409e7915663c47b10a0fa64db2e06a963c2fff25e421d`.
The local version increment is omitted from this contribution. The candidate
Release build succeeded with arm64e and x86_64 driver slices. Build 4 initially
remained attached during upgrade; a normal Mac restart completed replacement
before any build 5 audio testing.

| Check | Result and evidence |
| --- | --- |
| Enumeration and identity | One Project on the adapter chain above; independent Config ROM screenshot |
| Core Audio publication | Alive at 48 kHz with 10 inputs / 10 outputs |
| Silent start/stop | Two 3-second runs: 283/282 callbacks, no missing/repeated/backwards timestamps; [log](2026-09-07-silent-start-stop.txt) |
| Release after silent tests | No owner, GLOBAL_ENABLE=0, both ISO=-1, Internal 48 kHz locked; router tables and rounded mixer matrix unchanged; [report](2026-09-07-after-silent-test-dice-report.txt) |
| Stereo headphones | Three quiet 440 Hz left / 880 Hz right pairs over 24 seconds; listener confirmed correct sides and no distortion; [log](2026-09-07-headphone-tone-test.txt), [listening result](2026-09-07-headphone-listening-result.md) |
| Guitar input 1 | Confirmed playing window; channel 1 peak -26.04 dBFS, RMS -44.25 dBFS; [log](2026-09-07-guitar-input1-meter.txt), [result](2026-09-07-guitar-input1-result.md) |
| Guitar input 2 | Initial run reached full scale; lower-gain retest peaked -27.05 dBFS, RMS -46.62 dBFS; [initial log](2026-09-07-guitar-input2-meter.txt), [initial result](2026-09-07-guitar-input2-result.md), [retest log](2026-09-07-guitar-input2-low-gain-meter.txt), [retest result](2026-09-07-guitar-input2-low-gain-result.md) |
| GarageBand 10.4.14 | Owner confirmed recording and playback through the FireStudio; no take was exported or independently analysed |

The silent/tone clients targeted the exact Core Audio UID and did not change
default-device settings. Each confirmed guitar window delivered 939 callbacks
and 480,768 input frames over approximately 10 seconds, with no input-buffer
or timestamp errors. No input waveform was saved: meter results establish
signal/channel mapping, not subjective input quality. An earlier missed playing
window is excluded from confirmed results.

GarageBand confirmation is a user acceptance result. Its track input selection,
project rate and recorded file format were not independently captured. The
candidate exposes only 48 kHz, but this report does not infer GarageBand project
metadata from that fact.

Remaining tests: inputs 3–8 individually, physical Main/line output jacks,
S/PDIF, MIDI, other rates, sample-rate switching, sleep/wake, 5-minute/30-minute/
2-hour stability, calibrated latency, and complete IRM resource-pool equality.
A full retained-driver-log export and raw isochronous packet trace were not
obtained. The evidence establishes bounded operation on one unit, not general
production readiness.

## Host validation

120 tests passed across `AudioProfileRegistryTests`, `DiceProfileTests`,
`AmdtpDirectTxTests`, `DICETcatProtocolTests`, `DiceRuntimeDeviceConfigTests` and
`DICEDuplexBringupControllerTests`. Coverage includes exact model selection,
AM824/raw-PCM silence bytes and reused buffers, complete stream descriptors,
geometry drift, rate rejection before bus access, stale-cap invalidation and
ownership rollback with one completion callback. The AM824 default-silence
regressions were reproduced before the fix. These are host tests with DriverKit
stubs, not additional hardware runs.

## Behavioral references

- [Linux generic DICE stream setup](https://github.com/torvalds/linux/blob/df2908090cda368b01ff43709f51890076c56157/sound/firewire/dice/dice-stream.c#L488-L508)
  and [AM824 encoder/silence](https://github.com/torvalds/linux/blob/df2908090cda368b01ff43709f51890076c56157/sound/firewire/amdtp-am824.c#L148-L217).
- [Rust Project endpoint map](https://github.com/alsa-project/snd-firewire-ctl-services/blob/d4f8f2ba00fca75d8c361e3dcffccf7ad0010595/protocols/dice/src/presonus/fstudioproject.rs#L12-L80).
- [FFADO 2.5.0 source](https://ffado.org/files/libffado-2.5.0.tgz):
  `src/dice/presonus/firestudio_project.cpp`, `src/dice/dice_avdevice.cpp` and
  `src/libstreaming/amdtp/AmdtpTransmitStreamProcessor.cpp`.
- [Project owner's manual](https://pae-web.presonusmusic.com/downloads/products/pdf/FireStudioProject_OwnersManual_EN.pdf),
  printed pages 26 and 33, for Main/headphone and line-output relationships.

Reference implementation code was not copied into ASFireWire.
