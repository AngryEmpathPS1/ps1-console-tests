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
