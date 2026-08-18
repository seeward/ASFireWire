# M-Audio FireWire 1814 — BridgeCo Virtual UART shell captures

Verbatim stdout from the device's diagnostic shell, 2026-08-18, on a booted
`0x010071` unit at 48 kHz / S/PDIF with audio streaming.

| file | commands |
|---|---|
| `shell_20260818_sys_stat.txt` | `sys stat` — the per-stream isochronous counter table |
| `shell_20260818_clock_routing.txt` | `sys tgen`, `sys isodrv`, `sys isodrv 0`, `sys isodrv 1`, `sys avstat full`, `sc tgen`, `sc av`, `sc frmr`, `sc isodrv`, `avd printcfg`, `avd printav` |

These are the reference data behind `documentation/1814.md` §§8–9 and the parser
fixtures in `ASFWTests/BeBoBShellFixtures.swift`. Do not edit them to make a parser
pass — they are the device's output and therefore the specification.

Transport and command inventory: `documentation/BEBOB_VIRTUAL_UART_SHELL.md`.
