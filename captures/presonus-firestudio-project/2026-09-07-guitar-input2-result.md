# Guitar input 2 — initial mapping check reached full scale

The user confirmed “playing on 2” before this ten-second exact-UID Core Audio meter capture. Signal appeared on channel 2, reaching peak 1.0 (0 dBFS) and RMS -14.41 dBFS. Other analogue peaks were -75.55 to -80.34 dBFS, and digital channels 9/10 were zero.

939 callbacks delivered 480,768 input frames over 10.00533 seconds of input timestamps. Start/Stop/Destroy succeeded; no missing/malformed input buffers, nonfinite samples or timestamp anomalies were reported. Defaults stayed unchanged and the device returned idle.

Client/transport PASS does not mean a clean analogue recording: the signal reached digital full scale. The subsequent [lower-gain retest](2026-09-07-guitar-input2-low-gain-result.md) stayed below full scale; the exact cause of this initial peak was not established. No waveform was saved, so duration or audibility of clipping cannot be determined from the peak alone. No software gain/routing/clock changes were made.
