The GameShark's DB-25 comms link read by a Pico 2 W (Pico 3, pico-dbcap firmware) on the SCPH-1001, 2026-11-17 - the first time.
Wiring as cartrom/README "The DB-25 pipe" (DB-25 2-9 -> GP0-7, 10 STB <- GP9, 11 ACK -> GP8, 25 GND), first without the 1 kOhm resistors.
A first try read nothing (CARTIO 1F020010 FC, DBCAP bytes=0): the wires from pin 9 on were one hole off, STB on the Pico's ACK input.
sample 0 (the ACK edge's first sample), one boot: 35130 bytes, frames 1 11 3 2 18, ok 1 bad 4 (sums off by 1-14), diff=12957.
sample 1 ('S', the later sample), the next boot: 35120 bytes, frames 1 11 3 2 18, ok 5 bad 0, diff=14085.
So the data lines are still settling at the ACK edge and valid a few cycles later: sample 1 is the setting to use.
Note: the Pre-Boot frame's CARTIO 1F020010 still reads FC with STB high and bytes flowing - that byte is NOT the STB bit as the README guessed.
