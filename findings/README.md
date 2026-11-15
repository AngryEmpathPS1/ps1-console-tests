# Findings

What the logs in `consoles/` show, by subsystem. Each finding gives the numbers, the tests behind them, and the models and run counts.

- [The CPU and its bus](cpu.md): loads, the write queue, cold code, the DRAM refresh, slow devices, uncached code, exceptions, DMA, timers, GTE timing.
- [GPU, video timing and MDEC](gpu.md): the two GPUs, line and frame lengths, GPUSTAT, drawing costs, scan-out, copies, lines, texture costs, MDEC decode.
- [SPU](spu.md): the FIFO, transfer types, DMA, IRQ9, Key On, capture buffers, reverb, CD audio.
- [CD-ROM drive](cd.md): acknowledge times, sector delivery, the buffer, Pause, seeks, spin-up, speed changes, the lid, SCEx.
- [Controller ports, serial port, PocketStation, boot state](io.md).

## How far to trust this

Findings 48-60 of the GPU chapter, 50-52 of the CPU chapter and 41-43 of the SPU chapter came a night later, from one unit, and have not had the second pass.

An AI wrote these chapters. A separate AI pass then went through every finding, recomputed its numbers from the logs and recounted its runs. It found something to correct in 57 of the 204, mostly ranges and run counts, and those corrections are in. That is a check by another AI, not by a person. The logs are the evidence: if a finding and a log disagree, the log wins, and please say so.

Many results rest on few runs, and a few on one unit or one run. Each finding says which. Where a mechanism is given it is an explanation of the numbers, not something a log shows directly.

## Against psx-spx

Every finding was also compared with three texts: nocash's own, the `stable` branch of [psx-spx](https://github.com/psx-spx/psx-spx.github.io), and its `master` branch ("dev" here), read on 2026-10-30. Dev moves quickly and its pages say most recent changes are AI-written and not all reviewed.

| Chapter | Findings | Already there | There, this adds numbers, models or conditions | In none of the three | One of them says otherwise |
|---|---:|---:|---:|---:|---:|
| CPU and bus | 52 | 9 | 22 | 18 | 3 |
| GPU and MDEC | 60 | 13 | 24 | 16 | 7 |
| SPU | 43 | 5 | 23 | 9 | 6 |
| CD-ROM | 42 | 3 | 20 | 14 | 5 |
| Ports and serial | 26 | 4 | 16 | 2 | 4 |
| Total | 223 | 34 | 105 | 59 | 25 |

These are one reader's calls and "in none of the three" means a text search found nothing, so treat them as a guide.

### Where they disagree

Which text says what, and what the logs show. Details are in the finding named.

| Finding | Text | It says | The logs show |
|---|---|---|---|
| CPU-14 | dev | a RAM load with LDSCH off costs about 2 more | 4 more (one unit) |
| CPU-18 | all | a read waits for a queued store to the same location | it waits for a queued store within its own 64 bytes |
| CPU-37 | stable, dev | CAUSE.CE unchanged except on Coprocessor Unusable | opcode bits 26-27 after a bus error, as nocash says |
| GPU 2 | all | the old GPU fails DMA to VRAM above 10h-word blocks | none lost at 8 to 32 words; at 64 none in our 101 runs (25 on the old GPU), but some on a re-run (SCPH-1000 3 of 6, each SCPH-5501 1 of 9) |
| GPU 9 | all | GPUSTAT.31 always zero during VBlank | the low bit of the display Y |
| GPU 24 | nocash | old GPUs run at about 60% of the new speed | 1.0 to 3.0 times as slow, by primitive |
| GPU 34 | nocash | new-GPU masked copy figures | +0.2455 cycle a pixel from 64 wide |
| GPU 50 | dev | GPUSTAT.25 reads 0 at about 11 words queued | 9 counted, as nocash says: word 10 written (one unit) |
| GPU 51 | nocash | a quad leaves its last vertex in the FIFO | all its words but one vertex's (one unit) |
| GPU 56 | dev | 0000h texels cached 0.04 cycle a pixel faster on v2 | 0.019 (one unit) |
| SPU 6 | all | Rep types write eight halfwords | 4, 2 and 1 locations, differing by board group |
| SPU 11 | all | STATX bit 9 DMA read, bit 8 DMA write | bit 9 write, bit 8 read |
| SPU 28 | nocash | Key On copies start address to repeat address | it does not, as dev says |
| SPU 33 | all | writes to 1DBCh seem to have no effect on sound | on some units a write stopped voice 1's capture: one SCPH-7501 here, an SCPH-5501 and an SCPH-9002 on a re-run |
| SPU 41 | dev | polling bit 7 after CHCR goes idle keeps every byte | a 32-halfword block lost most of itself at 6 of 8 phases: bit 7 had not fallen yet (one unit) |
| SPU 42 | dev | exponential decrease stops at F001h; the Phase bit seems to have no effect | Phase 0 goes to 0; Phase 1 stops at a level set by the rate (one unit) |
| CD 18 | all | half-set flag about 1% for INT5 | 1 in 3,000 on the 5000, none on the 1001 (one run each) |
| CD 20 | nocash, stable | Read after Pause resumes at the most recent sector | one before, as dev says, on every locked start; the most recent on hand-loaded starts |
| CD 29 | all | GetlocL fails during a seek | during SeekL some units answer it; three vary run to run |
| CD 39 | all | SCEx strings only in the lead-in | found with the head at 00:02:00, not at 00:03:00 |
| CD 42 | all | RAM 060h increments at about 16 Hz | 840 to 910 a second |
| Ports: /ACK | all | /ACK at least 2 us wide | a Sony card and a PocketStation are narrower |
| Ports: COM0 | all | the COM_DELAY read formula | it mispredicts the first access (fit on three values) |
| Ports: SIO_STAT | all | bits 11+ a timer decrementing at 33 MHz | it does not read as one |
| Ports: BU codes | all | a write to protected sectors 16-55 returns FEh | FDh, on one PocketStation |

Re-run on six other consoles with these prebuilt tests (2026-11-15: SCPH-1000, SCPH-1001 with the new GPU, two SCPH-5501s, SCPH-7001, PAL SCPH-9002):
- Held: CPU-14, CPU-18, CPU-37, GPU 9, GPU 24, GPU 34, GPU 50, GPU 51, GPU 56, SPU 6, SPU 11, SPU 28, SPU 41, SPU 42, CD 18 and COM0. psx-spx dev now says so where it did not ([#244](https://github.com/psx-spx/psx-spx.github.io/pull/244), [#245](https://github.com/psx-spx/psx-spx.github.io/pull/245)). Dev's GPU 9 text was rewritten on 2026-11-12 and already agreed.
- Held on more units than ours: SPU 33 ([issue 2](https://github.com/AngryEmpathPS1/ps1-console-tests/issues/2)).
- Differed: GPU 2, where 64-word blocks did lose halfwords ([issue 1](https://github.com/AngryEmpathPS1/ps1-console-tests/issues/1); dev fixed in [#246](https://github.com/psx-spx/psx-spx.github.io/pull/246)).
- Not re-run yet: CD 20, CD 29, CD 39 and CD 42, which need a game disc, and /ACK, BU codes and SIO_STAT, which need a PocketStation or a loopback plug.
