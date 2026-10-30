# Controller ports, serial port, PocketStation, memory-control regions and boot state

Conventions. Times are CPU cycles (33.8688 MHz), counted with Timer 2 on the system clock. A polled event is resolved only to one pass of the test's polling loop (27-37 cycles; about 8 for the histogram test); each finding says which. "N logs" means N separate runs of one program.

Consoles: SCPH-1001 (BIOS 2.1), SCPH-5000 (2.2, Japan), SCPH-5501 (3.0, two units), SCPH-7001, SCPH-7501 and SCPH-9001 (4.1, US; the 9001 on more than one unit), SCPH-7502 (4.1, Europe). Every program was started from a memory-card loader (FreePSXBoot into Unirom), so "start state" below means after the BIOS shell and that loader, not at reset.

Devices, unless a finding says otherwise: a Sony DualShock answering as a digital pad in port 1 (every console, every SIO0 log), an SD-card replacement for a memory card (PSxMemCard) in slot 1, port 2 empty. Exceptions are named where they occur: a Sony memory card and an original digital pad in port 2 of one SCPH-5000, a Multitap on that SCPH-5000, a PocketStation in slot 2 of an SCPH-5501 (one log on an SCPH-7501). This device list comes from the setup notes; the logs show only what answered.

## SIO0: controller and memory-card ports

**The RX flag rises 1,334-1,495 cycles after a byte is written, on every model, whatever answers (or nothing).** At JOY_BAUD 0088h with 8-bit mode a bit is 136 cycles and the eight bits take 1,088. JOY_STAT.1 came 1,334-1,495 cycles after the JOY_DATA write for pads, memory cards and an empty port. The spread is about one bit time, which fits a baud timer that runs free and starts each byte on its next bit edge. Where the write's phase is fixed (byte 1 of a pad poll) the flag came at 1,416-1,431 in 99.6% of 10,752 polls.
Evidence: ack-per-device, pad-ack-hist; SCPH-1001, 5000, 5501, 7001, 7501, 7502, 9001; 49 and 42 logs (16 transactions and 256 polls per byte in each).

**A DualShock's /ACK is not a fixed delay: each byte falls in one of two windows about 230 cycles apart.**

| Poll byte (sent -> reply) | mean RX flag | earlier window | later window | mean /ACK |
|---|---|---|---|---|
| 1 (01h -> FFh) | 1,422 | 1,512-1,655 (18%) | 1,696-1,831 (82%) | 1,755 |
| 2 (42h -> 41h) | 1,402 | 1,560-1,607 (56%) | 1,800-1,839 (44%) | 1,689 |
| 3 (00h -> 5Ah) | 1,390 | 1,512-1,551 (80%) | 1,752-1,791 (20%) | 1,582 |
| 4 (00h -> FFh) | 1,400 | 1,480-1,527 (22%) | 1,712-1,759 (78%) | 1,685 |

Cycles from the TX write; 10,752 polls a row in 8-cycle bins (byte 2 has 0.4% between its windows). The mean /ACK comes 192-333 cycles after the mean RX flag. The pad does not acknowledge the 5th byte of the 01h 42h 00h 00h 00h poll. The share in each window changed between sessions, not between consoles: byte 1's earlier share was 11-16% in the first runner sessions (SCPH-5501, 7501, 1001, 9001) and 22-25% in every run from a few days later on, the SCPH-7501 itself going from 16% to 22%. The logs do not show which controller was plugged in.
Evidence: pad-ack-hist (42 logs); ack-per-device (49 logs: byte 1 +1,574..1,812, byte 2 +1,530..1,852, byte 3 +1,435..1,774, byte 4 +1,452..1,739); SCPH-1001, 5000, 5501, 7001, 7501, 7502, 9001; one DualShock in digital mode.

**The /ACK pulse is about 80-105 cycles for a pad and about 25-60 for a Sony memory card or a PocketStation.**

| Device | /ACK low for (cycles) |
|---|---|
| DualShock, digital mode | 79-105 |
| Sony memory card (one card) | 27-67, mostly 27-58 |
| PocketStation (one unit) | 26-57 (54-89 on the first byte of a command, up to 90 on the last byte of a read) |
| SD-card replacement (PSxMemCard) | 53-120 (67-120 on every byte but the ninth of a read, 53-105) |

Each figure is good to one polling pass. Differs from psx-spx for the Sony card and the PocketStation: nocash says the acknowledge signal is more than 2 us wide, dev and stable at least 2 us (68 cycles); the Sony card (27-67) and the PocketStation (26-57, 90 at most) are narrower. The pad (79-105) and the SD card agree with the "around 100 clk cycles" all three give. simias's logic-analyser figures, as quoted in rustation-ng's notes, are 24 or 35 cycles for a card and 90-100 for a pad; those are second-hand here, not checked at the source, and they agree.
Evidence: ack-per-device (49 logs, 7 models); pocket-cmds (4 logs) and pocket-write (2 logs) for the PocketStation; the Sony card is one card on one SCPH-5000.

**A Sony memory card read answers FLAG 08h, acknowledges 1,369-1,722 cycles after each byte, and does not acknowledge the byte after the sector address within 4,000 cycles.** Reading frame 0 (81h 52h 00h ...) the replies were FFh, 08h, 5Ah, 5Dh, 00h, 00h, 5Ch. /ACK: byte 1 +1,556..1,660, byte 2 +1,565..1,722, bytes 3-6 +1,369..1,618. The byte answered 5Ch got no /ACK inside the test's 4,000-cycle window in 7 of 7 logs (the test stops there). psx-spx places a late /ACK after the 5Ch byte; a PocketStation's is at +29,313..29,556 (see below). The card in slot 1 of every other log was an SD-card replacement and behaves differently: FLAG 00h, /ACK +1,334..1,474 (the ninth byte of the read +1,470..1,570), usually one polling pass (35-36 cycles) before the RX flag, the 5Ch byte acknowledged at once (+1,369..1,445), pulses 53-120. Its timings are not a Sony card's.
Open: how long the Sony card's sector-fetch delay is (more than 4,000 cycles; psx-spx gives circa 31,000 after the 7th byte, Sony cards only; not checked here).
Evidence: ack-per-device; SCPH-5000 only, 7 logs, one card in slot 2.

**An empty port returns FFh and no /ACK, and the RX flag still rises.** With nothing in port 2, byte 1 of a controller poll or of a card read: reply FFh, RX flag at +1,386..1,412, no /ACK within about 4,000 cycles.
Evidence: ack-per-device; SCPH-1001, 5501, 7001, 7501, 7502, 9001; 42 logs.

**A DualShock's configuration replies and its ANALOG button behave as psx-spx describes.** Replies after the first byte (which is FFh):
- 42h poll: digital 41 5A + 2 button bytes (5 bytes answered, the first 4 acknowledged, none after the 5th); analog (red) 73 5A + 2 buttons + 4 stick bytes (9 bytes).
- 45h outside config mode: 41, then no /ACK.
- 43h 01 enters config mode (its own reply is the poll's); every later reply starts F3 5A.
- 45h: 01 02 LED 02 01 00 (LED 00h digital, 01h analog). 46h 00: 00 00 01 02 00 0A. 46h 01: 00 00 01 01 01 14. 47h 00: 00 00 02 00 01 00. 4Ch 00: 00 00 00 04 00 00. 4Ch 01: 00 00 00 07 00 00. 44h 01 00: zeros, and the next 45h shows LED 01h. 4Dh 00 01 FF FF FF FF: FF x6 the first time, 00 01 FF FF FF FF the second.
- ANALOG pressed outside config mode: the ID byte flips (73h -> 41h; in one run back to 73h 0.39 s later) and the 5Ah header byte reads 00h from then on. Pressed inside config mode: the LED byte of 45h toggles and the header byte reads 00h. In both cases the header was 5Ah again only after the next 43h exchange. After 44h 01 03 (lock) a press at the prompt changed nothing (the log cannot show the press itself).
This is psx-spx's "Caution 3" (5Ah replaced by 00h after the button) seen on a console.
Evidence: pad-config; SCPH-9001, one DualShock, 3 runs: ANALOG outside config mode acted in 2 (the first run's window ended at 0.45 s, before any press); inside config mode in all 3.

Open: an original Sony digital pad in port 2 of the SCPH-5000, sharing the port with the Sony card, answered 00h to the first three bytes of the poll and acknowledged byte 4 at +249..1,774 and byte 5 at +109..181 after their writes, in 7 of 7 logs. Cause unknown (the pad, a contact, or the card on the same lines); not repeated with the card removed.
Evidence: ack-per-device; SCPH-5000, 7 logs.

## Multitap (SCPH-1070)

All rows below are one run of one program with one tap and one pad (in slot B, answering 41h 5Ah); none was repeated. Treat them as a first observation.

**A request sent through slot B starts full mode while slot A is empty, and a full read addressed to the empty slot A then returns all 35 bytes.**
- 02 42 01 00 00 (slot B, REQ=1): FF 41 5A FF FF, /ACK after bytes 1-4.
- Next, 01 42 01 followed by 42h 00 x7 in each slot's command positions: FF 80 5A, then 8 bytes per slot: slot A FFh x8, slot B 41 5A FF FF FF FF FF FF, slots C and D FFh x8. The first full read after the request was already valid. A second full read with REQ=1 was again 35 bytes (no alternation with a short reply); one with REQ=0 was also 35 bytes.
- The same request through the empty slot A: one byte (FF), no /ACK; a full read after it also one byte.
- A full read addressed 02h returned the same 35 bytes as one addressed 01h.
- A full read with 00h in the slot-command positions was still 35 bytes; the next full read (42h again) stopped after 4 bytes (FF 80 5A FF); the one after that was 35 bytes.
Differs from the older psx-spx table (nocash and stable: REQ=1 on every read alternates short garbage with long reads). The dev text's revised table already gives long data every time with 42h and a short reply after 00h, as seen here; after the short reply dev predicts another short one, and here a REQ=0 read returned all 35 bytes. Requests need a pad in slot A in all three; here a request through slot B worked, and one through empty slot A failed.
Evidence: multitap-modes; SCPH-5000, 1 run.

**A full read shows a button in the same or the adjacent transfer, and the tap acknowledges its own bytes about 380-400 cycles sooner than a pad.** With passthrough and full reads alternating about 2 ms apart, eight presses were seen by the full read at +1, -1, +1, -1, -1, +1, +1, +1 transfers from the passthrough read (+1 five times, -1 three times). A tap that replied with the previous read's data would give +1 or +3, never -1, so the replies are fresh; the 00h-command row shows the commands taking effect one read late.

| /ACK after the write (cycles) | |
|---|---|
| passthrough byte to the pad | 1,495-1,803 |
| address byte of a full read | 2,559 or 2,699 (one bit time apart) |
| bytes 4-6 of a full read | 1,495-1,777 |
| bytes 7-34 of a full read | 1,215-1,327, median 1,271 (196 samples) |

No /ACK follows byte 35.
Open: a tap with four pads and cards behind it (not yet run), and the tap's effect on a card's /ACK.
Evidence: multitap-modes; SCPH-5000, 1 run.

## Memory control, BIOS ROM and expansion regions

**Read cost by region, with the registers as the BIOS leaves them.** Cycles per load instruction (ps1-tests cpu/access-time: a loop of 100 `nop; load` minus a loop of 100 `nop`, at three widths), identical on every console:

| Region (address) | 8-bit | 16-bit | 32-bit |
|---|---|---|---|
| BIOS ROM (BFC00000h) | 7.06 | 12.94 | 24.94 |
| Expansion 1 (1F000000h), nothing attached | 6.94 | 13.07 | 25.07 |
| Expansion 2 (1F802000h), nothing attached | 10.99 | 25.95-25.99 | 56.02-56.04 |
| Expansion 3 (1FA00000h), nothing attached | 6.07-6.11 | 5.95 | 9.95-9.96 |
| CD-ROM registers (1F801800h) | 8.00 | 13.99-14.00 | 25.93-25.95 |
| SPU registers (1F801DAAh) | 17.99 | 17.99 | 38.94 |
| Main RAM (data load) | 5.14-5.24 | 5.07-5.15 | 5.07-5.15 |
| Scratchpad | 1.01 | 1.01-1.05 | 0.94 |
| I_STAT, JOY_STAT, SIO_STAT, GPUSTAT, RAM_SIZE | 2.9-3.1 | 2.9-3.1 | 2.9-3.2 |

BIOS ROM and Expansion 1 behave as 8-bit buses (a word is 7 + 3 x 6); Expansion 3 as 16-bit. Logs from sessions in which an earlier test had changed the delay registers read 2-12 cycles more on 16- and 32-bit reads of the BIOS, Expansion 1 and 2 and the CD-ROM registers (for example BIOS 14.94 and 30.94), so the rows above hold only for the BIOS-default registers. The test prints the fraction left-justified (3.8 means 3.08). Its loop lets about 2 cycles of every load hide behind the next instructions, so back-to-back figures run 2 higher. "Nothing attached" comes from the setup notes.
Evidence: cpu/access-time (ps1-tests); 27 logs at default registers; SCPH-1001, 5000, 5501, 7001, 7501, 7502, 9001 (SCPH-1001 and 7502 one log each).

**COM_DELAY's COM0 field changes a CD-ROM or SPU register read only through "Use COM0", and not as psx-spx's formula says.** Cycles per load:

| Register | COM_DELAY (COM0) | 8-bit | 16-bit | 32-bit |
|---|---|---|---|---|
| CD-ROM, Delay/Size 00020843h (Use COM0 off) | 00031125h (5) | 8.04 | 14.04 | 26.04-26.06 |
| same | 00001323h (3), 00001325h (5), 0000132Ch (12) | 7.85 | 13.85 | 25.85 |
| CD-ROM, 00020943h (Use COM0 on) | 00001323h (3) | 7.85 | 15.85 | 31.85 |
| same | 00031125h (5), 00001325h (5) | 7.85 | 17.85 | 37.85 |
| same | 0000132Ch (12) | 13.79 | 30.79 | 64.79 |
| SPU, 200931E1h (Use COM0 on) | 00001323h (3) | 17.85 | 18.04 | 35.85 |
| same | 00001325h (5) | 17.85 | 18.04-18.06 | 37.85 |
| same | 0000132Ch (12) | 23.79 | 23.98 | 50.79 |

Each further access of one load costs COM0 + 5 cycles on the CD-ROM register (8, 10 and 17 for COM0 = 3, 5 and 12), which is psx-spx's COM0 - 1 term. The first access does not follow that term: the 8-bit read was unchanged between COM0 = 3 and 5 (7.85) and 5.94 higher at 12, where psx-spx's formula would add 2 and then 6. The SPU register, run with Use COM0 on only, behaves the same way. The fit rests on three COM0 values.
Evidence: mem-delay; 28 logs from sessions with the BIOS-default registers; SCPH-1001, 5000, 5501, 7001, 7501, 7502, 9001; rows identical except +/-0.02 in two cells.

**The BIOS Delay/Size register's size field sets the ROM window: bus error beyond it, a mirror of the 512 KB ROM between 512 KB and the window.** 1F801010h reads 0013243Fh (size 13h, 512 KB) after boot on every console. Reads sampled at 11 offsets for each size written (a bus error is an exception):

| Size field | 0-3FFFCh | 40000h-7FFFCh | 80000h-FFFFCh | 100000h-1FFFFCh | 200000h-3FFFCh |
|---|---|---|---|---|---|
| 12h (256 KB) | ROM | bus error | bus error | bus error | bus error |
| 13h (512 KB) | ROM | ROM | bus error | bus error | bus error |
| 14h (1 MB) | ROM | ROM | mirror | bus error | bus error |
| 15h (2 MB) | ROM | ROM | mirror | mirror | bus error |
| 16h (4 MB) | ROM | ROM | mirror | mirror | mirror |

"Mirror" is the word at the offset modulo 512 KB. At 12h even addresses inside the 512 KB chip fault. Agrees with psx-spx ("exceeding the selected size causes an exception"); nothing read as FFh or 0.
Evidence: bios-window; SCPH-1001, 5000, 5501, 7001, 7501, 7502, 9001; 31 logs, all identical.

**A bus error is taken after the access, not at it: EPC lands 1-4 opcodes later, depending on the code's cache state.** Tested with a faulting access (BIOS window overrun, unmapped I/O gaps 1F801024h and 1F801078h, the unmapped 08000000h region), CAUSE ExcCode 7 (data bus error), followed by eight flag-setting opcodes. EPC in opcodes past the access; the opcode at EPC does not run:

| Code | EPC |
|---|---|
| uncached | +1 |
| cached, line in the cache | +2 |
| cached, line being refilled, access at word 0 / 1 / 2 / 3 of its line | +2 / +1 / +4 / +3 |

CAUSE.CE (bits 29-28) is non-zero except in the +9 cell below, and follows the opcode type at the point of the exception (1 or 2 with this test's alternating ORI/XORI). Two cells depend on how the program was started, not on the console: a load from 08000000h at word 0 of a warm line landed at +2 in 13 logs (the first two days, one SCPH-5000 session and the second SCPH-5501) and at +5 in the other 18; at word 2 it landed at +9 in 7 logs (that SCPH-5000 session and the second 5501) and +2 in 24. Within one session every run agreed.
Evidence: bios-window; SCPH-1001, 5000, 5501, 7001, 7501, 7502, 9001; 31 logs, 5 accesses each.

## Boot state and model differences

**At program start, on every model, these registers hold the same values.** Taken after the BIOS shell and the memory-card loader had run:
- BIOS ROM Delay/Size 0013243Fh (31 of 31 logs). COM_DELAY 00031125h, CD-ROM Delay/Size 00020843h, SPU Delay/Size 200931E1h (28 of 28 logs from sessions where no earlier test had changed them).
- DPCR 08009899h (43 of 43 logs), DICR 00840000h and MDEC status 80040000h (16 of 16 logs), I_MASK 0000h (25 logs not taken in the loader's debug mode).
- SPUCNT C085h and SPU transfer control 0004h (43 of 43 logs).
- RAM_SIZE (1F801060h) 00000B88h, and 00000B80h on the SCPH-1001. Bits 16-31 of a 32-bit read are not the register: they appear to be the last word on the bus (8001h in most logs, 2484h in the first sessions). A 32-bit read of I_MASK shows the same: in cached code the data of the last RAM load or Timer 2 store (not of an I_MASK store), in uncached code the instruction fetched two places after the read.
Agrees with psx-spx, which excepts PU-7 and early PU-8 from the bit-3 rule (nocash) and says the BIOS writes 0B80h there (dev, stable). The SCPH-1001 is PRID 1 with the old GPU; the only tension is psx-spx filing its drive version (95/07/24 C1) under LATE-PU-8, and no log gives the board label.
Open: no register was read before the BIOS ran, so reset-time values of the memory-control registers are unmeasured. The base-address and delay registers of Expansion 1, 2 and 3 (1F801000h-1F80100Ch, 1F80101Ch) were not read on any console.
Evidence: mem-delay (28), bios-window (31), spu-health (43), mdec-in-hang (16), machine-id (83), imask-upper (31), bus-latch (16); SCPH-1001, 5000, 5501, 7001, 7501, 7502, 9001.

**What each unit reports about itself.**

| Model | BIOS (version, date, region) | CRC32 | RAM_SIZE low half | CPU PRID | Drive controller (date, version) |
|---|---|---|---|---|---|
| SCPH-1001 | 2.1 07/17/95 A | AFF00F2F | 0B80 | 1 | 95/07/24 C1 |
| SCPH-5000 | 2.2 12/04/95 J | 24FC7E17 | 0B88 | 2 | 95/07/24 C1 |
| SCPH-5501 | 3.0 11/18/96 A | 8D8CB7E4 | 0B88 | 2 | 97/01/10 C2 |
| SCPH-7001 | 4.1 12/16/97 A | 502224B6 | 0B88 | 2 | 97/08/14 C2 |
| SCPH-7501 | 4.1 12/16/97 A | 502224B6 | 0B88 | 2 | 98/06/10 C3 |
| SCPH-7502 | 4.1 12/16/97 E | 318178BF | 0B88 | 2 | 98/06/10 C3 |
| SCPH-9001 | 4.1 12/16/97 A | 502224B6 | 0B88 | 2 | 99/02/01 C3 |

Outside the CPU, GPU, SPU and drive, nothing else separated the seven in these logs: SIO0 timing, the region read costs, the COM0 effect, the BIOS window, bus-error timing and the start-up registers above read alike (a few cells differ by session, not by console). PRID 1 (the older CPU) and RAM_SIZE bit 3 set the SCPH-1001 apart; the SCPH-5000 differs in its PAL clock (next).
Evidence: machine-id; 83 logs, all seven models (SCPH-5501 two units).

**One SCPH-5000 stops its video timing in PAL mode.** With GP1(08h) in NTSC at every width Timer 1 stepped 18 times in 3,000 polls and VBlank arrived (263 lines a frame). In PAL at every width Timer 1 did not step, no VBlank came in 1.5 million polls (about 0.5 s, 29 NTSC frames), and GPUSTAT bit 20 was set. The drawing engine kept working: a 64x64 fill finished in 50 polls (47 in NTSC) and GP1(10h) answered. Only the video clock stops. psx-spx ("PAL/NTSC Color Mods") cautions that boards have pads for both a PAL and an NTSC oscillator and the missing one is grounded or tied to the other, varying board to board; this unit behaves like a grounded pad. pal-dots ran to completion in PAL on the other six models.
Open: a second SCPH-5000 has not been tested; this is shown for one board, not for the model.
Evidence: pal-probe, pal-probe2 (1 log each, all waits bounded); SCPH-5000, one unit. pal-dots: SCPH-1001, 5501, 7001, 7501, 7502, 9001.

## SIO1: the serial port

All SIO1 rows below used a loopback plug (TXD to RXD, DTR to DSR, RTS to CTS) in the serial socket, so the port received what it sent.

**SR.0 rises one bit time after the TX_DATA write, SR.2 at the end of the frame, and the byte reaches the RX FIFO just after the middle of the first stop bit.** Bit time = BAUD x factor cycles (0024h x 16 = 576, 0010h x 64 = 1,024, 00D8h x 16 = 3,456, 0100h x 1 = 256). Polled at about 30 cycles a pass:

| Format, factor, BAUD | bit (cycles) | SR.0 after the write | SR.2 after SR.0 | byte in the FIFO after SR.0 |
|---|---|---|---|---|
| 8N1, MUL16, 0024h | 576 | 563-620 | 10.0 bits | 9.63-9.65 bits (5,548-5,556) |
| 8N2 | 576 | 598-610 | 11.0 | 9.59-9.63 |
| 8N1.5 | 576 | 598-612 | 10.5 | 9.59-9.63 |
| 8O1 and 8E1 | 576 | 598-612 | 11.0 | 10.61 |
| 5N1 (5Ah arrives as 1Ah) | 576 | 570-607 | 7.0 | 6.60-6.67 |
| 8N1, MUL64, 0010h | 1,024 | 1,027-1,056 | 10.0 | 9.52-9.54 |
| 8N1, MUL16, 00D8h | 3,456 | 3,375-3,573 | 10.0 | 9.62-9.63 |
| 8N1, MUL1, 0100h | 256 | 181-192 (0.72 bit) | 9.94-9.97 | 10.95-11.00 (after SR.2) |

So: the start bit begins one bit time after the write (0.72 in MUL1) and SR.0 sets then; a parity bit adds one bit; extra stop bits do not delay the FIFO; the FIFO delay is 9.5 bits plus two receiver ticks (bit/16 in MUL16, bit/64 in MUL64). An odd BAUD in MUL1 (0101h) ran like 0100h. BAUD 0001h x 16 (16 cycles a bit) delivered the byte intact; the poll was too coarse to time it.
The start is not aligned to the baud timer: writes made 45-47, 157-167, 301-311, 445-455 and 733-743 cycles after a BAUD write set SR.0 at 640-669 cycles after the write in every case (a fifth log, handshake wires crossed, gave 638-672). A start on the next half bit (288) or whole bit (576) would step by those amounts.
Evidence: sio1-loopback; SCPH-9001, 4 runs (plus one with the handshake crossed).

**The RX FIFO holds 8 bytes; a ninth replaces the last; an emptied FIFO repeats its last byte into the unused entries; a 16-bit read removes one entry and a 32-bit read four.** Nine bytes unread: SR.4 set, reads 01 02 03 04 05 06 07 09 then 00. Six bytes (11-16): reads 11 12 13 14 15 16 16 16 then 00. With six bytes in, a 16-bit read returned two entries (low byte first) and removed one; a following 32-bit read returned four and removed four, leaving one. This is nocash's description in psx-spx; the logs agree.
Evidence: sio1-loopback; SCPH-9001, 4 runs, identical.

**Interrupts: RX re-triggers on acknowledge while the FIFO still holds data; TX interrupts while idle; DSR interrupts when DSR rises.** With the RX interrupt on at one byte: SR.9 and I_STAT.8 set when a byte arrives; acknowledging through SIO_CTRL.4 with a byte left raised I_STAT.8 again, with the FIFO empty it stayed clear. The TX interrupt was pending at once while idle. The DSR interrupt did not fire with DTR off and fired when DTR was turned on (DSR follows DTR through the plug).
Evidence: sio1-loopback; SCPH-9001, 3 runs (a fourth run's interrupt rows are void: the loader's handler took the interrupts).

**With the plug complete, DSR (SR.7) follows DTR and CTS (SR.8) follows RTS; with the RTS-CTS link missing, CTS read 1 whatever RTS did.** Complete plug: nothing on 0/0; DTR on 1/0; RTS on 0/1; both 1/1 (DSR/CTS). With only TXD-RXD and DTR-DSR joined, DSR still followed DTR and CTS read 1 in all four states, and the transmitter ran normally, so an unconnected CTS input counts as asserted.
Evidence: sio1-loopback; SCPH-9001, 2 runs with a complete plug, 2 with RTS-CTS open.

**CTS holds back a start from idle; a byte already running finishes, and a byte queued behind it follows it.** One run:
- A byte written with CTS off: SR.0 never rose in 40,000 cycles and nothing arrived; SR.2 read set again 56 cycles after the write. Turning CTS on, SR.0 rose 622 cycles later and the byte arrived 6,160 cycles after CTS (SR.2 at 6,376).
- CTS dropped 1,798 cycles after a write (three bits into the byte): the byte still completed (SR.2 6,356, RX 6,156).
- A second byte queued at +704 with CTS dropped just after it: the first arrived at 6,142 and the second at 11,940, straight behind it.
Open: whether CTS is sampled at the write or only for a start from idle.
Evidence: sio1-loopback; SCPH-9001, 1 run.

**Rewriting BAUD around a byte never mis-sampled it, which fits one baud generator for receiver and transmitter.** BAUD written before the start bit (155-207 and 481-486 cycles after TX_DATA) or inside the frame (1,576-1,585), moving from 576 to 288 cycles a bit or from 288 to 1,152: in every row the byte came back as written with no parity, overrun or framing flag. A four-byte burst queued at 32 cycles a bit with BAUD rewritten to 1,152 cycles 155 cycles later also arrived whole (49 0B FF C4).
Open: a loopback cannot show whether the frame already on the wire changed its bit time, only that the two ends never disagreed.
Evidence: sio1-mismatch; SCPH-7501, 1 log with 2 passes, identical.

**SIO_STAT bits 11-31 do not read as a 15-bit counter running down from the BAUD reload at the CPU clock.** Straight after writing BAUD 0100h (MUL16) the field read 111-125 at 31-42 cycles and 84-105 at 563-577 cycles; a 33.8688 MHz down-counter reloaded with 2,048 would read about 2,010 and 1,475. psx-spx (all three texts) describes this field as a timer decrementing at 33 MHz (nocash 15-bit; dev and stable 15-21 bit).
Open: what the field counts. It fell 20-27 in about 535 cycles in four runs (roughly one count per 20-27 cycles), and the first reading differed between runs.
Evidence: sio1-loopback; SCPH-9001, 4 runs.

## PocketStation seen from the console

One PocketStation, in slot 2 of an SCPH-5501, with the SD-card replacement in slot 1 as the control.

**It answers the standard card commands and psx-spx's extra commands with the bytes psx-spx lists, but each byte's /ACK is later than a card's: by about 560 cycles on the first byte of a command and about 280 on later bytes.**

| Command | Replies after the first FFh |
|---|---|
| 53h Get ID | FLAG 5A 5D 5C 5D 04 00 00 (80h last, no /ACK) |
| 58h | FLAG 02 01 01 (5th byte unacknowledged) |
| 5Ah | FLAG 12h, then 18 bytes: directory index (2), four ComFlags bytes, serial number (4), BCD day, month, year, century, second, minute, hour, weekday; the clock advanced one second between reads a second apart |
| 5Bh FUNC 00h | FLAG FF 00 08, then 8 date/time bytes, then FF |
| 52h | the usual frame read |

First byte of a command acknowledged at +1,877..1,970 (the SD card: +1,323..1,440); later bytes +1,449..2,394 (the SD card: +1,341..1,530). Two delays stand out: the fourth byte of 5Bh FUNC 00h (answered 00h, the parameter length) is acknowledged at +3,366..3,609; in a 52h read the byte answered 5Ch is acknowledged at +29,313..29,556 (0.87 ms), where the SD card needs +1,368..1,422. FLAG read 08h throughout the read-only commands and 00h after the first successful write.
Evidence: pocket-cmds; 4 logs of 4 repetitions per command, one unit on SCPH-5501.

**A write returns 47h, a bad checksum 4Eh and writes nothing, and the read straight after the third write failed.** Writing frame 1023 (a valid checksum): end status 47h, read back as written; data bytes acknowledged at +1,549..2,076. A second, different pattern sent with its checksum XOR FFh: 4Eh, and the frame still held the first pattern. Reads straight after the first write and after the bad-checksum write succeeded; the read straight after writing the original back (47h) failed in both runs, and the same read repeated afterwards succeeded.
Open: how long the unit stays busy after a write.
Evidence: pocket-write; one unit on SCPH-5501, 2 runs.

**With ComFlags bit 10 set (5Dh 00 00 00), a write to frames 16 and 55 returned FDh and changed nothing; frames 15 and 56 returned 47h.** After 5Dh 00 01 00 the same writes to 16 and 55 returned 47h. The 5Dh replies were FF, FLAG, 03, 00, 00, 00. Differs from psx-spx: it gives FEh for writes to this write-protected region (sectors 16-55) and FDh for a running file's directory entries. FDh was returned for the protected region here; the directory case was not tested in these logs.
Evidence: pocket-protect; one unit on SCPH-5501, 1 run (each frame written back with its own bytes, then read back unchanged).

**A unit in stand-by does not answer the port at all.** A PocketStation that had been put back in stand-by gave FFh and no /ACK for byte 1 of every command, exactly like an empty slot; reads of frames failed. After it was woken with its button it answered every command at once. The stand-by and the button come from the session notes; the logs show only the silence and the later answers.
Evidence: pocket-cmds (3 of 7 logs: two on SCPH-5501, one on SCPH-7501), pocket-protect (1 log of failed reads, then the successful log after waking); one unit.

## Not measured

No console log covers: a GunCon or Justifier, a mouse, a neGcon or a DualShock 2; a Multitap with four pads and cards behind it; SIO0 interrupt timing (every SIO0 test polled with interrupts off); SIO1 between two consoles; anything attached to the parallel or expansion ports; registers before the BIOS runs; the PSone and the SCPH-1000.
