# sio1-stim: the SCPH-1001's SIO1 under the bridge Pico's exact stimulus (2026-11-24, the console fuzzer, for AngryEmpath)

pico-serial v7.05 STIM on board B port 3 (tools/sio1stim.js), hwtests/sio1-stim through tools/sio1run.js; the console at BAUD 18,
MODE 4Eh (115,200 8N1), CTRL 05h. Logs: console-logs/2026-11-24-SCPH-1001-sio1stim-r2/-r3/-r4 (each with stim.json, the Pico's edges).

- r2, 16 bytes at 115,200: all 16 read exactly; RX ready (SR.1) every 2,921-2,950 cycles (ten bits = 2,940; the spread is this test's
  ~29-cycle poll). The Pico's own TX: 80/80 edges, worst 0.033 us. CTS reaches the 1001 (SR.8 = 1 with the Pico's asserted
  drive); DSR does not (SR.7 stayed 0 through --dsr=0:0,2000:1).
- r3, bad stop bits on bytes 2 and 5 (of 11 22 33 44 55 66 77 88): read 11 22 33 88 55 66 EE 88. The bad-stop byte itself is read
  right and sets SR.5 (stays until acknowledged); the NEXT byte reads as itself shifted left one bit (44h -> 88h, 77h -> EEh): the
  receiver took the low stop bit as the next start bit. RX ready came 2,747 cycles after the bad byte's, not ~2,940.
  (This core models a restart after a bad stop: SIO1_RX_RESTART, fitted on two consoles; this is the exact-source check.)
- r4, 20 bytes back to back, nothing read until the window ended: drained 11 22 33 44 55 66 77 then 78h - the first seven and
  the NEWEST byte in the last slot - with SR.4 (overrun) set. As hwtests/sio1-cannon found between two consoles.
- Rate sweep (2026-11-25; console-logs/2026-11-25-SCPH-1001-sio1stim-ratesweep): the console at 288 cycles a bit (117,600), the Pico's
  ten bytes (55 15 35 0D 2D 00 80 FF 7F 01) back to back at 117,600 x (1 + d). Clean from d = -4.5% to +4.5%. At -5.0% (sender slow)
  every byte from the second on reads right but with SR.5 (bad stop); at -6.0% bytes gain bit 7 (55 -> D5, 80 -> 00 ...). At +5.0%
  (sender fast) the first byte is right, then bit 7 set and SR.5 from the second byte on (35 -> B5, 0D -> 8D); +6.0% worse. The
  slow side alone puts the stop bit's sample 122-136 cycles after the start edge is seen (0.42-0.47 of a bit; the exact middle is
  144) - the earlier-than-middle sampling sio1-sample saw. The fast side fails only from the second byte, so it also holds how soon
  the receiver looks for the next start (SIO1_RX_LAG). The emulator's side of this needs the stimulus replayed into its SerialPort.

## The emulator's side (2026-11-25): every run replayed (tools/sio1emu.js)

tools/sio1emu.js runs this exe on the virtual console with an emulated Pico in the cart slot (tools/sio1emu-preload.js): the same 16
settings bytes over its dbio.h, and on MARK A1h the same waveform on the emulated SIO1 line (SerialPort.postFrame), CTS on and DSR
off as the 1001 read them. Both records are aligned on their first byte in (the console's start follows the PC hearing the arm, a
different few ms each run) and compared after it: values, SR.3-5, the time from the first byte to each read.

- v0.377.382 + next (stack 4a1072714e): -4.5% .. +4.5%, r2, r3 (bad stops) and r4 (overrun) - every value and flag the console's;
  reads within the ~29-cycle poll (|d| mean 6-22). The edges: -5% the first byte's bad stop (this core) against the console's clean
  first byte (a draw of the tick phase, U[0, 18)); -6% / +6% two and three values (single runs: not modelled from one run).
  **+5%: the console reads byte 0 clean and every later byte with a bad stop and bits 6-7 shifted (00 -> 80, 80 -> C0, 7F -> BF);
  this core read all ten clean (7 values, 9 flags off).** Its search for the next start resumed AT the stop sample, where the fast
  sender's next start edge comes 7-25 cycles later; the console's does not see it there.
- **experiments/land_rxresume.js (SIO1_RX_RESUME, candidate):** after a good stop too, the search resumes SIO1_RX_RESTART (2) ticks
  after the stop sample, by the line's level - the restart this core already makes after a bad one. +5%: 2 values, 1 flag (the
  console's pattern a byte later, then the same to the byte from byte 4); +4.5%: read times |d| mean 6.2 -> 0.8, max 29 -> 2 (every
  byte-ready within 2 cycles); the other 16 runs unchanged to the cycle. On the emulated pair (tools/linkemu.js), where v0.377.304
  rejected a level search after every frame (17 rows of 22): sio1-rates 18/22 rows vs the 7501, 19/22 vs the 5501, sio1-sample
  1,245 / 1,251 of 1,312 - unchanged (experiments/sio1emu/ratescore.js, experiments/samplescore.js).
