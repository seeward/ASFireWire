# First audible headphone test — passed

The user confirmed: “lower on the left and higher on the right - no distortion”. Headphones were connected directly to FireStudio Project, with Phones initially down and then raised manually for listening.

The exact-device client addressed ASFW-000A920402D07FAC at 48 kHz. It sent a fixed 24-second sequence, capped below -36 dBFS: five seconds of silence, three alternating lower-440-Hz left / higher-880-Hz right pairs with fades and silent gaps, then silence. Channels 3–10 remained zero. No input samples were read or saved; no device, routing, clock or volume property was changed.

Software: 2,252 callbacks; all 1,152,000 sequence frames submitted; output sample-time span 24.01067 seconds; no missing, duplicate or backwards output timestamps. Start/Stop/Destroy succeeded. Device returned alive and idle at 48 kHz, and default input/output/system-output IDs were unchanged.

This verifies short, quiet stereo playback through the existing mixer to the headphone jack. It does not independently validate Main/line jacks, analogue recording, other channels, 44.1 kHz or long-run stability. See the [validation summary](README.md) for subsequent input checks and GarageBand confirmation.
