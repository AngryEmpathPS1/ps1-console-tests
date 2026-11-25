SCPH-1000 (3475), GameShark, cart ROM 0918b2ea, 2026-11-25: CART START test 4 with fuzz batch 20500 (tools/fuzzcartstart.js --allow-v4).
The console went silent after the soft reset (twice today: 13:19 on 61016b99, 16:11 on 0918b2ea); the emulator runs it to 63 lines.
After AngryEmpath's RESET (boot 23, WARM 1):
- padring.txt: boot 22 (the hung one) reached the break, pad 41 5A FF FF twice, no escape.
- records.txt: 80190F00h 'CSP1' (the program accepted: 80100800h, 11000h bytes, from RAM); 80190F80h 'CSR1', the log 26DFh bytes,
  word 10 = 1: the test ENDED. So the batch ran to its end; the hang is after - CART START's hand-back to the runner (its log at
  80140000h was overwritten by the next boot's runner).
- boot-after-reset.bin: that boot's frames; the stage 11h hang record holds the cartmode stub at 80100000h, HANG.STEP 9Fh (stale),
  RUNNER.STEP 524B0105h ("the test came back, DONE going out": the runner before the soft reset).
- The cart's flash read 8 times whole through the runner (experiments/cart/cartreads.js): 0 bytes differ - not a cart contact fault.
Same shape as boots 6 / 7 (14:49 soft reset, a RESET): the break reached, the pad clean, then no runner and SIO1 held low.
