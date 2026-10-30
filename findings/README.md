# Findings

What the logs in `consoles/` show, by subsystem. Each finding gives the numbers, the tests behind them, and the models and run counts.

- [The CPU and its bus](cpu.md): loads, the write queue, cold code, the DRAM refresh, slow devices, uncached code, exceptions, DMA, timers, GTE timing.
- [GPU, video timing and MDEC](gpu.md): the two GPUs, line and frame lengths, GPUSTAT, drawing costs, scan-out, copies, lines, texture costs, MDEC decode.
- [SPU](spu.md): the FIFO, transfer types, DMA, IRQ9, Key On, capture buffers, reverb, CD audio.
- [CD-ROM drive](cd.md): acknowledge times, sector delivery, the buffer, Pause, seeks, spin-up, speed changes, the lid, SCEx.
- [Controller ports, serial port, PocketStation, boot state](io.md).

## How far to trust this

An AI wrote these chapters. A separate AI pass then went through every finding, recomputed its numbers from the logs and recounted its runs. It found something to correct in 57 of the 204, mostly ranges and run counts, and those corrections are in. That is a check by another AI, not by a person. The logs are the evidence: if a finding and a log disagree, the log wins, and please say so.

Many results rest on few runs, and a few on one unit or one run. Each finding says which. Where a mechanism is given it is an explanation of the numbers, not something a log shows directly.

## Against psx-spx

Every finding was also compared with three texts: nocash's own, the `stable` branch of [psx-spx](https://github.com/psx-spx/psx-spx.github.io), and its `master` branch ("dev" here), read on 2026-10-30. Dev moves quickly and its pages say most recent changes are AI-written and not all reviewed.

| Chapter | Findings | Already there | There, this adds numbers, models or conditions | In none of the three | One of them says otherwise |
|---|---:|---:|---:|---:|---:|
| CPU and bus | 49 | 7 | 21 | 18 | 3 |
| GPU and MDEC | 47 | 13 | 19 | 11 | 4 |
| SPU | 40 | 5 | 22 | 9 | 4 |
| CD-ROM | 42 | 3 | 20 | 14 | 5 |
| Ports and serial | 26 | 4 | 16 | 2 | 4 |
| Total | 204 | 32 | 98 | 54 | 20 |

These are one reader's calls and "in none of the three" means a text search found nothing, so treat them as a guide.

### Where they disagree

Which text says what, and what the logs show. Details are in the finding named.

| Finding | Text | It says | The logs show |
|---|---|---|---|
| CPU-14 | dev | a RAM load with LDSCH off costs about 2 more | 4 more (one unit) |
| CPU-18 | all | a read waits for a queued store to the same location | it waits for a queued store within its own 64 bytes |
| CPU-37 | stable, dev | CAUSE.CE unchanged except on Coprocessor Unusable | opcode bits 26-27 after a bus error, as nocash says |
| GPU 2 | all | the old GPU fails DMA to VRAM above 10h-word blocks | no halfword lost at 8, 16, 32 or 64 words |
| GPU 9 | all | GPUSTAT.31 always zero during VBlank | the low bit of the display Y |
| GPU 24 | nocash | old GPUs run at about 60% of the new speed | 1.0 to 3.0 times as slow, by primitive |
| GPU 34 | nocash | new-GPU masked copy figures | +0.2455 cycle a pixel from 64 wide |
| SPU 6 | all | Rep types write eight halfwords | 4, 2 and 1 locations, differing by board group |
| SPU 11 | all | STATX bit 9 DMA read, bit 8 DMA write | bit 9 write, bit 8 read |
| SPU 28 | nocash | Key On copies start address to repeat address | it does not, as dev says |
| SPU 33 | all | writes to 1DBCh seem to have no effect on sound | on one SCPH-7501 a write stopped voice 1's capture |
| CD 18 | all | half-set flag about 1% for INT5 | 1 in 3,000 on the 5000, none on the 1001 (one run each) |
| CD 20 | nocash, stable | Read after Pause resumes at the most recent sector | one before, as dev says, on every locked start; the most recent on hand-loaded starts |
| CD 29 | all | GetlocL fails during a seek | during SeekL some units answer it; three vary run to run |
| CD 39 | all | SCEx strings only in the lead-in | found with the head at 00:02:00, not at 00:03:00 |
| CD 42 | all | RAM 060h increments at about 16 Hz | 840 to 910 a second |
| Ports: /ACK | all | /ACK at least 2 us wide | a Sony card and a PocketStation are narrower |
| Ports: COM0 | all | the COM_DELAY read formula | it mispredicts the first access (fit on three values) |
| Ports: SIO_STAT | all | bits 11+ a timer decrementing at 33 MHz | it does not read as one |
| Ports: BU codes | all | a write to protected sectors 16-55 returns FEh | FDh, on one PocketStation |
