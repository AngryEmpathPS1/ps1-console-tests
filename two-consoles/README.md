# Two consoles at once

Sessions with two consoles joined as a link cable joins them, through the bridge Pico's relay: the second SCPH-5501 on the bridge's first port, the SCPH-7501 on its second (`tools/linkrun.js` in the emulator's repository runs both and records the relay). Each console's log is `<test>-<model>.txt`; a `-swap` session is the same test with the two roles reversed (the 7501 as A). `relay.bin` is the relay's record of everything that crossed: 8-byte records - A5h, a type (0 a byte A sent, 1 a byte B sent, 2 A's lines changed, 3 B's, 4 the relay started, 5 records lost, 6 refused), the Pico's microsecond clock (4 bytes, little endian), the byte or the lines, its error bits (1 framing, 2 parity, 4 overrun). The byte stamps are good to a few microseconds; the line stamps run 30-100 us late (the relay polls the lines), and the relay's inputs pull a console's open-collector RTS / DTR up through ~50K where a link cable's far console has 1K, so a line going ON rises slower through the relay than through a cable.

| session | test | logs | relay |
|---|---|---:|---|
| [2026-11-07-cannon](2026-11-07-cannon/) | sio1-cannon | 2 | yes |
| [2026-11-07-clocks](2026-11-07-clocks/) | sio1-clocks | 3 | yes |
| [2026-11-07-pingpong](2026-11-07-pingpong/) | sio1-pingpong | 3 | yes |
| [2026-11-07-rates](2026-11-07-rates/) | sio1-rates | 2 | yes |
| [2026-11-07-rates-swap](2026-11-07-rates-swap/) | sio1-rates | 2 | yes |
| [2026-11-07-sample](2026-11-07-sample/) | sio1-sample | 2 | yes |
| [2026-11-07-sample-swap](2026-11-07-sample-swap/) | sio1-sample | 2 | yes |
| [2026-11-07-sample64](2026-11-07-sample64/) | sio1-sample64 | 2 | yes |
| [2026-11-07-sample64-swap](2026-11-07-sample64-swap/) | sio1-sample64 | 2 | yes |
| [2026-11-07-transfer](2026-11-07-transfer/) | sio1-transfer | 2 | yes |
| [2026-11-07-transfer-swap](2026-11-07-transfer-swap/) | sio1-transfer | 2 | yes |
| [2026-11-07-transfer2](2026-11-07-transfer2/) | sio1-transfer2 | 2 | yes |
| [2026-11-07-transfer2-swap](2026-11-07-transfer2-swap/) | sio1-transfer2 | 2 | yes |
| [2026-11-08-transfer2-drain](2026-11-08-transfer2-drain/) | sio1-transfer2 | 2 | yes |
| [2026-11-08-transfer2-drain-swap](2026-11-08-transfer2-drain-swap/) | sio1-transfer2 | 2 | yes |
| [2026-11-08-transfer2-edge](2026-11-08-transfer2-edge/) | sio1-transfer2 | 2 | yes |
| [2026-11-08-transfer2-edge-swap](2026-11-08-transfer2-edge-swap/) | sio1-transfer2 | 2 | yes |
