# Guitar input 1 — confirmed signal mapping

The user explicitly confirmed “playing on 1” before the ten-second capture began. The exact FireStudio Core Audio UID was used at 48 kHz, with zero output PCM and no device-property changes.

Channel 1 peaked at -26.04 dBFS, RMS -44.25 dBFS. Other analogue channels peaked between -78.77 and -80.67 dBFS; digital channels 9/10 were zero. This establishes the connected guitar signal on the expected Core Audio input 1 within this meter-level test.

939 callbacks delivered 480,768 input frames; the input timestamp span was 10.00533 seconds. No missing/malformed input buffers, nonfinite samples, missing/repeated/backwards input timestamps or default-device changes were reported. Start, stop and cleanup succeeded and the device returned idle.

No waveform was saved, so recorded sound quality is not established. This check does not validate other physical inputs, 44.1 kHz or long-run stability. An earlier eight-second run was inconclusive because the user may have missed its playing window; that run is excluded from this evidence folder. See the [validation summary](README.md) for subsequent checks.
