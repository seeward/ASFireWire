# FireStudio Project: 44.1 kHz and S/PDIF validation, 2026-09-08

The owner confirmed digital clock lock and audible test tones over coaxial
S/PDIF from the FireStudio Project to a Roland VM-3100 DIGITAL IN A (DIN-A)
at **44.1 kHz**. Six short silent trials at 44.1/48 kHz and two 24-second tone
runs completed on local candidate build 8, with transport progress and successful
cleanup in the retained driver logs. This establishes bounded synchronization
and playback on one unit; the limitations below are part of the result.

## Candidate provenance

- Same FireStudio Project, MacBookPro18,3 / M1 Pro and Apple Thunderbolt/FireWire
  adapter chain as the [September 7 capture](README.md#capture-provenance).
- App/driver 0.3.0, local build 8. The executable actually running after the
  normal macOS restart was checked at 07:57:00 UTC against the signed candidate.
  Driver SHA-256:
  `591fe51b261cd7d8cea007c1be78856fad1057e2d0bcd33c8d901a65d8f79f87`.
- The local candidate was built from base `ac8a124` plus the uncommitted feature
  and correction changes. Its app-local build counter and generated version
  metadata are omitted from this PR; a build from the PR will have a different
  binary hash. The hardware evidence describes candidate build 8, not a separate
  hardware run of a subsequently rebuilt PR artifact.
- Release build succeeded. The driver includes x86_64 and arm64e; the app includes
  x86_64 and arm64. Signatures verified and entitlements matched repository files.
  The 53 build warnings were in unchanged source files.
- The [unchanged 44.1 kHz DICE report](2026-09-08-dice-report-44100.txt) was exported
  at 08:00:03 UTC, SHA-256
  `35c7a79f8d6d4ccb850253b6269993cf85ccca19c5d8af34ca37fce7bc0f17d2`.
  Its embedded driver build timestamp is reproduced as reported, not treated as
  the compile time. It confirms Internal clock, selected/nominal/measured
  44,100 Hz, locked, no owner, GLOBAL_ENABLE=0, both ISO streams disabled and
  one stream of 10 PCM + 1 MIDI per direction.

## Changes and failed candidates

The Project profile now accepts 44.1 and 48 kHz while retaining its 48 kHz default
and exact 10 PCM + 1 MIDI geometry. Other rates and geometry drift are rejected.
The existing generic AM824 path supplies the rate-specific FDF and cadence.
A rejected clock change no longer silently becomes the next StartIO rate.
The exact-UID probe derives its tone budget, frequencies and fades from the
inspected 44.1/48 kHz rate; it writes playback 1/2 and zeroes channels 3–10.

Build 6's first silent **48 kHz** test failed after 104 callbacks, before the
44.1 kHz test. The driver crashed on a recursive hardware-access lock in the
transmit watchdog's fatal path. The correction releases the diagnostic lock
before stopping and retains the Faulted state and DMA buffers until the normal
ACTIVE-clear shutdown barrier succeeds. A host test reproduced the pre-fix
lockup using a five-second timeout.

Build 7 avoided that crash but still lost transmit interrupts while Core Audio
callbacks continued. Device cleanup timed out, and a subsequent start incorrectly
took an already-running path. The next correction reads fresh per-context event
masks after global acknowledgment and clears each mask once before dispatch,
removing the saved-mask duplicate clears. This independently implemented order
was checked against the [Linux OHCI interrupt handler](https://github.com/torvalds/linux/blob/28924df2a08f440c73991b83028032c901de2ae4/drivers/firewire/ohci.c#L2061).
The adapter reports revision 8; the revision-6-only Linux no-MSI quirk was not
applied, and MSI policy and interrupt rearming were unchanged.

A reservation helper also distinguishes starting, running, stopping and failed
cleanup. Failed cleanup retains the device reservation and blocks overlapping
starts/clock/recovery work; only a genuinely running reservation permits an
idempotent start. Operation epochs reject stale completions. The existing DICE
asynchronous timeout/cancellation design is unchanged.

Build 8 passed the bounded retest below. The earlier IRQ stall did not recur in
those runs; this does not establish its sole cause or prove long-run resolution.

## Hardware results

ASFW and Audio MIDI Setup were closed during active trials. The probe selected
the exact FireStudio UID, with a 512-frame callback buffer. Silent runs were
three seconds each; rate changes were made while idle. Each probe run had
continuous output timestamps, no missing/repeated/backwards timestamps and
successful start, stop and callback destruction.

| Trial, in order | Rate (Hz) | Callbacks | Frames written | IT packets / interrupts |
| --- | ---: | ---: | ---: | ---: |
| Silent 1 | 48,000 | 283 | 144,896 | 25,182 / 4,107 |
| Silent 2 | 48,000 | 282 | 144,384 | 24,942 / 4,138 |
| Silent 3 | 44,100 | 260 | 133,120 | 25,116 / 4,133 |
| Silent 4 | 44,100 | 260 | 133,120 | 25,153 / 4,078 |
| Silent 5, return to 48 kHz | 48,000 | 282 | 144,384 | 24,918 / 4,047 |
| Silent 6, return to 44.1 kHz | 44,100 | 260 | 133,120 | 25,045 / 4,108 |
| S/PDIF tone | 44,100 | 2,068 | 1,058,816 | 193,002 / 30,552 |
| S/PDIF tone, requested repeat | 44,100 | 2,068 | 1,058,816 | 192,948 / 30,962 |

All eight retained driver logs were checked for actual start/stop and the earlier
interrupt-watchdog, fatal-stop and asynchronous-timeout failures; none of those
failures appeared. The [lifecycle excerpts](2026-09-08-driver-lifecycle-excerpts.txt)
retain selected verbatim lines with source log hashes and original line numbers.
They are excerpts, not a full-log proof of the absence of other diagnostics.
The [six complete silent probe logs](2026-09-08-silent-start-stop.txt) preserve
client metrics independently of that driver-log review.

Each tone run used five seconds of lead-in silence, alternating 440/880 Hz tones
on host playback 1/2, a -36 dBFS host peak and 10 ms fades, within a 24-second
sequence. The 1,058,400-frame sequence budget completed; the final callback's
remaining frames were silent. See the [first run](2026-09-08-spdif-tone.txt) and
[requested repeat](2026-09-08-spdif-tone-repeat.txt).
The owner reported “ye locked now” after selecting DIN-A, then requested a repeat
and confirmed “yrs i can hear the test tones”. No separate distortion assessment
was supplied. The FireStudio was left alive and idle at 44.1 kHz.

Default device IDs were unchanged within every active trial. Default output
changed across the rate/UI phase; its final identity was not established.
No default-device setter was used by the probe, and this result makes no claim
about the cause of that system-level change.

## PR checkout verification

The updated PR checkout independently rebuilt and passed the same **237 targeted
host tests across 15 executables**. Its Release build also succeeded with
x86_64/arm64e driver slices and 53 warnings in unchanged source files.
The PR retains the repository's build counter (4); the tested local install used
build 8. Runtime/test source matches the hardware checkout apart from the profile
validation comment and excluded local version metadata. No hardware was opened
or reconfigured while preparing this PR update.

## Host coverage and limits

The build 8 consolidated run passed **237 tests across 15 executables**. Coverage
includes exact profile selection and geometry, both rates and rejected-rate
recovery, ten-lane AM824 payloads/MIDI/silence, fractional cadence, descriptor
handling, watchdog/shutdown barriers, interrupt ordering and failed-stop
reservation decisions. These use DriverKit host stubs. Reservation tests exercise
the production decision helper rather than a full DriverKit AudioCoordinator.
The separate probe validation passed 3,806 generator checks, 4,988 synthetic
callback/gate checks, seven meter groups and eight CLI rejection cases, with
warnings-as-errors and ASan/UBSan checks passing.

The saved mixer routes Mixer8/9 to both S/PDIF outputs and applies the same sum
to each, including playback 1/2. No router, mixer coefficient or flash changes
were made. The tone labels in the client log describe host channels; they do
**not** establish independent digital left/right routing. This trial captured
neither a waveform nor a digital bitstream, so it does not establish bit-perfect
transfer or measured signal quality. Digital input, MIDI, inputs 3–8 individually,
physical Main/line jacks, other rates, sleep/wake, long-run endurance, calibrated
latency and full IRM resource-pool equality remain untested. Mixer controls and
headphone-mix management are future work, outside this contribution.
