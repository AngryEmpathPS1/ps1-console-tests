SCPH-1000 (3475) GameShark DB-25 on the two-DB-25 Pico's port 3 (3450), wired by AngryEmpath 2026-11-25.
tools/cartbringup.js --console=3475 --pico=3450 --boot: PROBE / BACKUP / WIRES (22 of 22 ACK pulses) / DATA (00..ff) / BOOT (5 frames
over the DB-25) all PASS on the first run. experiments/fuzz-all/db25bringup.sh 3475 3450 SCPH-1000: status ok (ovr 0 drop 0), dbio
00..ff, mark cost B5 gaps 17.2-17.6 cycles, 60 cases marks on/off 57 identical (3 cold 1-2 cycles - the same 3 of 60 as marks off run
twice: the 1000's own cold spread, the 1001 had none), fzclock 60 paired, outside = 0.999970 x (burn + T2) + 89.1, residual MAD 0.79
cycles (the 1001: 3.7-5.3); feed 20 cases, 18 as the table (2 cold -2). tools/bridgeports.json's port-3 row names the SCPH-1000.
