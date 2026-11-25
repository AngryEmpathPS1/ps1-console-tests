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
