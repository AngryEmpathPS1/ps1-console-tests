# Coverage

How many logs each test has on each console. Generated from the file names in `consoles/`.

| test | SCPH-1001 | SCPH-5000 | SCPH-5501 | SCPH-5501/unit-2 | SCPH-7001 | SCPH-7501 | SCPH-7502 | SCPH-9001 | SCPH-9001/borrowed-1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ack-per-device | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| at-split | 3 | 7 | 3 | 1 | 6 | 5 | 1 | 3 | 2 |
| bios-window | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| biu-dram |  |  |  | 2 |  |  |  |  |  |
| biu-sched |  |  |  | 2 |  |  |  |  |  |
| bus-latch | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| cd-2x-burned | 1 |  | 1 |  | 2 | 1 |  | 1 |  |
| cd-ack-getloc | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-ack-times | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-ack-window | 7 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-adpbusy | 1 |  | 1 |  | 2 | 1 |  | 1 |  |
| cd-boot-read | 4 | 2 | 3 | 3 | 3 | 4 | 2 | 3 |  |
| cd-boot-steps | 7 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-cmds | 7 | 7 | 13 | 3 | 7 | 16 | 2 | 10 | 3 |
| cd-da-capture | 1 |  | 1 |  | 2 | 1 |  | 1 |  |
| cd-game-steps | 7 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-gameday | 2 |  | 2 |  | 2 | 2 |  | 2 |  |
| cd-getlocl-seek | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-hc05-tick | 3 | 2 | 4 | 3 | 4 | 5 | 4 | 3 |  |
| cd-head | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-hold-after | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-hold-loc | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-hold-locl | 4 | 2 | 4 | 3 | 4 | 5 | 2 | 5 |  |
| cd-init | 4 | 6 | 6 | 2 | 4 | 8 | 4 | 6 | 2 |
| cd-init-after | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-init-args | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-init-far | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-int1-spread | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-irq-glitch | 1 | 1 | 2 | 2 | 2 | 4 | 1 | 2 | 2 |
| cd-leadout | 4 | 1 | 4 | 1 | 1 | 4 | 1 | 3 |  |
| cd-lid |  |  |  |  |  | 3 |  |  |  |
| cd-lid-sweep |  |  |  |  |  | 3 |  |  |  |
| cd-lid-sweep2 |  |  |  |  |  | 3 |  |  |  |
| cd-lid-sweep3 |  |  |  |  | 3 | 3 | 3 | 3 |  |
| cd-lid-sweep4 |  |  |  |  | 3 |  |  |  |  |
| cd-pastend | 2 | 1 | 3 | 1 | 1 | 3 | 1 | 3 |  |
| cd-pause | 11 | 7 | 12 | 3 | 7 | 17 | 2 | 10 | 3 |
| cd-queue | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-race | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-readtoc | 7 | 7 | 13 | 3 | 7 | 16 | 2 | 10 | 3 |
| cd-reann | 7 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-scex | 11 | 7 | 12 | 3 | 7 | 17 | 2 | 10 | 3 |
| cd-scex-hold | 4 | 7 | 4 | 3 | 7 | 7 | 2 | 4 | 3 |
| cd-scex-play | 2 |  | 2 |  | 2 | 2 |  | 2 |  |
| cd-seek | 4 | 6 | 6 | 2 | 4 | 8 | 4 | 6 | 2 |
| cd-seek-cmd | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-seek-coast | 7 | 2 | 6 | 3 | 6 | 8 | 7 | 6 |  |
| cd-seek-hist | 3 | 2 | 3 | 3 | 6 | 2 | 2 | 3 |  |
| cd-seek-long | 3 | 2 | 3 | 3 | 2 | 7 | 7 | 7 |  |
| cd-seek-map | 7 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-seek-mid | 8 | 2 | 3 | 3 | 7 | 7 | 7 | 7 |  |
| cd-seek-path | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-seek-path2 | 8 | 2 | 7 | 3 | 7 | 7 | 7 | 7 |  |
| cd-seek-path3 | 15 | 8 | 9 | 8 | 8 | 8 | 15 | 8 |  |
| cd-seek-path4 | 8 | 8 | 9 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path5 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path6 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path7 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path9 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-pause | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-seek-phase | 3 | 2 | 3 | 3 | 3 | 2 | 2 | 3 |  |
| cd-seek-phase2 | 3 | 2 | 3 | 3 | 3 | 2 | 2 | 3 |  |
| cd-seek-phase3 | 3 | 2 | 3 | 3 | 3 | 2 | 2 | 3 |  |
| cd-sled | 3 | 2 | 3 | 3 | 2 | 7 | 7 | 7 |  |
| cd-small-reads | 7 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-speed | 4 | 6 | 6 | 2 | 4 | 8 | 4 | 6 | 2 |
| cd-speedpause | 4 | 6 | 6 | 2 | 4 | 8 | 4 | 6 | 2 |
| cd-spinup-phase | 3 | 2 | 3 | 3 | 6 | 7 | 7 | 7 |  |
| cd-spinup-state | 3 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-spinup-trend | 3 | 3 | 3 | 3 | 3 | 5 | 3 | 3 |  |
| cd-spunup | 4 | 6 | 4 | 2 | 4 | 6 | 1 | 4 | 2 |
| cd-store | 1 | 1 | 2 | 2 | 2 | 4 | 1 | 2 | 2 |
| cd-xa | 1 |  | 2 |  | 2 | 3 |  | 3 |  |
| cd-xa-switch | 1 |  | 1 |  | 2 | 3 |  | 2 |  |
| cd-xa-switch2x | 7 |  | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-xa-switch2x-ff7 |  | 6 |  |  |  |  |  |  |  |
| cd-xa-switch2x-ff7eu |  |  |  |  |  |  | 2 |  |  |
| cdrom_getloc | 1 |  | 2 | 1 | 1 | 2 |  | 2 |  |
| cdrom_timing | 7 | 4 | 9 | 2 | 7 | 13 | 2 | 10 | 3 |
| clut-cost | 6 | 7 | 10 | 1 | 6 | 12 | 1 | 8 | 2 |
| code-in-io-safe | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| cold-bracket | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| cold-branch | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| cold-window | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| concurrent-dma | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| counter-bracket | 1 | 7 | 5 | 1 | 6 | 10 | 1 | 6 | 2 |
| counter-store | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| cpu-code-in-irq | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| cpu-extras | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| cpu-loop-tick | 2 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick10 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick11 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick12 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick13 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick14 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick15 |  |  | 3 | 1 |  |  |  | 2 |  |
| cpu-loop-tick16 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick17 |  |  |  | 1 |  |  |  | 2 |  |
| cpu-loop-tick18 |  |  |  | 1 |  |  |  | 2 |  |
| cpu-loop-tick2 | 2 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick3 | 2 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick4 | 2 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick5 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick6 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick7 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick8 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu-loop-tick9 |  |  | 2 | 1 |  |  |  | 2 |  |
| cpu_access-time | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| cpu_code-in-io | 1 | 1 | 2 |  | 1 | 2 | 1 | 2 |  |
| cpu_cop | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| dma-arb | 6 | 6 | 6 | 1 | 6 | 8 | 1 | 5 | 2 |
| dma-contention | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| dma-rates | 6 | 7 | 8 | 1 | 6 | 11 | 1 | 7 | 1 |
| dma_chopping | 6 | 6 | 10 | 1 | 6 | 14 | 1 | 10 | 2 |
| dma_dpcr | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| dma_otc-test | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| draw-area | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| fifo-burst | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| fill-gap | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| first-try-app-running |  |  | 1 |  |  |  |  |  |  |
| first-try-asleep |  |  | 1 |  |  |  |  |  |  |
| fix-shadow | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| fix-shadow2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| fix-shadow3 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| fourth-try-stop-polled |  |  | 1 |  |  |  |  |  |  |
| gp0-bit25 |  |  |  | 3 |  |  |  |  |  |
| gp0-bit25b |  |  |  | 3 |  |  |  |  |  |
| gp0-bit25c |  |  |  | 3 |  |  |  |  |  |
| gp0-overrun |  |  |  | 3 |  |  |  |  |  |
| gp0-poll | 2 | 7 | 2 | 1 | 6 | 2 | 1 | 2 |  |
| gp0-store | 2 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| gp1-reset |  |  |  | 3 |  |  |  |  |  |
| gpu-a0-ready | 6 | 6 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| gpu-a0-size | 1 | 7 | 6 | 1 | 6 | 6 | 1 | 4 | 2 |
| gpu-abort | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| gpu-batch | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-batch2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-batch3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-c0-cold | 3 | 7 | 6 | 1 | 6 | 5 | 1 | 3 | 2 |
| gpu-c0-cold2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| gpu-c0-datel | 6 | 6 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| gpu-c0-gaps | 6 | 6 | 6 | 1 | 6 | 6 | 1 | 4 | 2 |
| gpu-c0-ready | 1 | 7 | 6 | 1 | 6 | 6 | 1 | 4 | 2 |
| gpu-c0-size | 2 | 7 | 6 | 1 | 6 | 4 | 1 | 2 | 2 |
| gpu-cmd-ready | 1 | 6 | 6 | 1 | 6 | 4 | 1 | 2 | 2 |
| gpu-copy-480i |  |  |  | 3 |  |  |  |  |  |
| gpu-copy2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-copy3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-copy4 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-cost-sweep | 6 | 7 | 8 | 1 | 6 | 13 | 1 | 8 | 2 |
| gpu-degen |  |  |  | 3 |  |  |  |  |  |
| gpu-degen2 |  |  |  | 3 |  |  |  |  |  |
| gpu-dmabs | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-dmabs2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-dmastream | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-dmastream2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-dmastream3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-dmastream4 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-dmastream5 |  |  |  | 3 |  |  |  |  |  |
| gpu-drift | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-fill-480i |  |  |  | 3 |  |  |  |  |  |
| gpu-height | 2 | 7 | 6 | 1 | 6 | 6 | 1 | 6 |  |
| gpu-irq-chain | 3 | 7 | 6 | 1 | 6 | 5 | 1 | 3 | 2 |
| gpu-lines | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-lines2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-outside |  |  |  | 5 |  |  |  |  |  |
| gpu-outside2 |  |  |  | 3 |  |  |  |  |  |
| gpu-outside3 |  |  |  | 3 |  |  |  |  |  |
| gpu-outside4 |  |  |  | 3 |  |  |  |  |  |
| gpu-outside5 |  |  |  | 3 |  |  |  |  |  |
| gpu-phase | 2 | 6 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-phase2 | 2 | 6 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-rect-cached |  |  |  | 3 |  |  |  |  |  |
| gpu-rect-x |  |  |  | 3 |  |  |  |  |  |
| gpu-rect-x2 |  |  |  | 3 |  |  |  |  |  |
| gpu-scan3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-scan4 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-scan5 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-scan6 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-scanline | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-scanline2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-scanline3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-scanout | 3 | 7 | 6 | 1 | 6 | 8 | 1 | 3 | 2 |
| gpu-scanout2 | 2 | 7 | 6 | 1 | 6 | 9 | 1 | 6 |  |
| gpu-semi-shape | 3 | 7 | 6 | 1 | 6 | 5 | 1 | 3 | 2 |
| gpu-shape-time | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time10 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time11 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time12 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time13 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time14 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time15 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time16 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time17 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-shape-time18 | 2 | 7 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time19 | 2 | 7 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time20 | 2 | 7 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time21 | 2 | 7 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time22 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time23 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time24 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time25 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time26 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time27 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time28 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time29 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time30 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time31 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time32 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time33 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time34 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time35 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time36 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time37 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time38 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time39 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time4 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time40 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time41 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time42 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time43 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time44 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time45 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time46 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time47 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time48 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time49 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time5 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time50 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time51 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time52 | 2 | 6 | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time53 | 2 |  | 6 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time54 | 2 | 6 | 2 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time55 | 2 | 6 | 2 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time56 | 2 | 1 | 2 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time57 | 2 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| gpu-shape-time58 | 2 | 1 | 2 | 1 | 6 | 1 | 1 | 2 |  |
| gpu-shape-time59 | 2 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| gpu-shape-time6 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time60 | 2 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time61 | 2 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time62 | 2 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time63 | 2 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time64 | 2 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time65 | 1 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time66 | 1 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time67 | 1 |  | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time68 | 1 | 1 | 1 | 1 |  | 6 | 1 | 1 |  |
| gpu-shape-time68c | 1 | 1 | 1 | 1 |  | 4 | 1 | 1 |  |
| gpu-shape-time68e | 1 | 1 | 1 | 1 |  | 12 | 1 | 1 |  |
| gpu-shape-time69 | 1 | 1 | 1 | 1 |  | 4 | 1 | 1 |  |
| gpu-shape-time7 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time70 | 1 | 1 | 1 | 1 |  | 4 | 1 | 1 |  |
| gpu-shape-time71 | 1 | 1 | 1 | 1 |  | 4 | 1 | 1 |  |
| gpu-shape-time72 | 1 | 1 | 1 | 1 |  | 4 | 1 | 1 |  |
| gpu-shape-time73 | 3 | 1 | 1 | 1 |  | 3 | 1 | 1 |  |
| gpu-shape-time74 | 1 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time75 | 1 |  | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time76 | 1 | 1 | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time77 | 1 | 1 | 1 | 1 |  | 3 | 1 | 1 |  |
| gpu-shape-time78 | 1 |  | 1 | 1 |  | 2 | 1 | 1 |  |
| gpu-shape-time8 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-shape-time9 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-tail | 2 | 7 | 6 | 1 | 6 | 3 | 1 | 6 |  |
| gpu-tail2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-tail3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-tail4 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-tail5 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-tail6 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-tail7 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 6 |  |
| gpu-tail8 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-tail9 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-tex-opaque | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| gpu-thin | 3 | 7 | 6 | 1 | 6 | 8 | 1 | 3 | 2 |
| gpu-transp-cached |  |  |  | 3 |  |  |  |  |  |
| gpu-tri-ready | 3 | 7 | 6 | 1 | 6 | 5 | 1 | 3 | 2 |
| gpu-vbl | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-vbl2 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-vbl3 | 2 | 7 | 6 | 1 | 6 | 2 | 1 | 2 |  |
| gpu-version | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| gpu_bandwidth | 6 | 6 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| gpu_gp0-e1 | 1 | 6 | 6 | 1 | 6 | 6 | 1 | 4 | 2 |
| gpu_mask-bit | 1 | 6 | 6 | 1 | 6 | 6 | 1 | 4 | 2 |
| gpuinfo-latch | 1 | 7 | 6 | 1 | 6 | 6 | 1 | 4 | 2 |
| gpustat | 1 | 6 | 6 | 1 | 6 | 6 | 1 | 4 | 2 |
| gpustat31 | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| gte-hold | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| gte-irgb | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| gte-latch-cached | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| gte-sqr-align | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| gte-write-delay |  |  |  | 3 |  |  |  |  |  |
| gte_test-all | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| hold-read | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| icache-tagmatch | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| imask-upper | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| io-chain | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| io-pair2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| io-pairs | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| io-ram | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| io-shadow | 1 | 1 | 1 | 1 | 2 | 4 | 1 | 2 | 2 |
| io-store-shadow | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| line-fill | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| line-fill2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| line-split | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| load-fill | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| load-fill2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill3 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill4 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill5 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill6 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-pairs | 1 | 6 | 5 | 1 | 6 | 12 | 1 | 6 | 2 |
| load-use | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| machine-id | 9 | 7 | 27 | 1 | 6 | 16 | 1 | 13 | 2 |
| mdec-color | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| mdec-glitch24 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| mdec-idct | 6 | 6 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| mdec-idct2 | 2 | 1 | 2 | 1 | 2 | 6 | 1 | 2 |  |
| mdec-in-hang | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| mdec-leftover | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mdec-mono-out | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mdec-padding | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mdec-qbias | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mdec-reset | 1 | 6 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mdec-round | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mdec-scale | 2 | 7 | 2 | 1 | 6 | 6 | 1 | 2 |  |
| mdec-scale2 | 2 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| mdec-step24 | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mdec-time | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| mem-delay | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| miss-grid | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| movie-browser-depths |  |  |  |  |  |  | 1 |  |  |
| movie-browser-fixed |  |  |  |  |  |  | 1 |  |  |
| movie-browser-hangs |  |  |  |  |  |  | 1 |  |  |
| movie-browser-timing |  |  |  |  |  |  | 1 |  |  |
| movie-disc-box1 |  |  | 1 |  |  |  | 1 |  |  |
| movie-disc-box2 |  |  | 1 |  |  |  | 1 |  |  |
| movie-disc-hunt1 |  |  |  |  |  |  | 1 |  |  |
| movie-disc-hunt2 |  |  |  |  |  |  | 1 |  |  |
| movie-disc-hunt3 |  |  |  |  |  |  | 1 |  |  |
| movie-disc-hunt4 |  |  |  |  |  |  | 1 |  |  |
| muldiv-overlap | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| multitap-modes |  | 1 |  |  |  |  |  |  |  |
| pad-ack-hist | 6 | 6 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| pad-config |  |  |  |  |  |  |  | 3 |  |
| pal-dots | 6 |  | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| pal-line | 6 |  | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| pal-probe |  | 1 |  |  |  |  |  |  |  |
| pal-probe2 |  | 1 |  |  |  |  |  |  |  |
| pocket-cmds |  |  | 6 |  |  | 1 |  |  |  |
| pocket-protect |  |  | 1 |  |  |  |  |  |  |
| pocket-start |  |  | 1 |  |  |  |  |  |  |
| pocket-write |  |  | 2 |  |  |  |  |  |  |
| ram-banks | 2 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| ram-store-io | 2 | 7 | 2 | 1 | 6 | 2 | 1 | 2 |  |
| ram-store-io2 | 2 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| ram-store-io3 | 2 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| ram-store-io4 | 2 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| ram-store-io5 | 2 | 7 | 2 | 1 | 6 | 1 | 1 | 2 |  |
| ram-store-io6 | 2 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| ram-store-io7 | 2 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| refresh-cold | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-io | 3 | 7 | 3 | 1 | 6 | 5 | 1 | 3 | 2 |
| refresh-io2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-io3 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-io4 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-store |  |  | 3 | 1 |  |  |  | 2 |  |
| refresh-store2 |  |  |  | 3 |  |  |  |  |  |
| refresh-store3 |  |  |  | 3 |  |  |  |  |  |
| refresh-store4 |  |  |  | 3 |  |  |  |  |  |
| refresh-store5 |  |  |  | 3 |  |  |  |  |  |
| refresh-store6 |  |  |  | 3 |  |  |  |  |  |
| refresh-store7 |  |  |  | 3 |  |  |  |  |  |
| refresh-warm | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| second-try-after-stop |  |  | 1 |  |  |  |  |  |  |
| semi-cost | 6 | 7 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| seq-miss | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| seq-miss2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| shadow-miss | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| shadow-miss2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sio1-loopback |  |  |  |  |  |  |  | 8 |  |
| sio1-mismatch |  |  |  |  |  | 1 |  |  |  |
| slow-b2b | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| spu-busy | 6 | 6 | 6 | 1 | 6 | 9 | 1 | 6 | 2 |
| spu-curvol | 1 | 6 | 2 | 1 | 6 | 4 | 1 | 2 | 2 |
| spu-decode-ahead | 6 | 6 | 6 | 1 | 6 | 10 | 1 | 6 | 2 |
| spu-dma | 6 | 6 | 8 | 1 | 6 | 11 | 1 | 7 | 1 |
| spu-exp-neg |  |  |  | 3 |  |  |  |  |  |
| spu-fifo | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| spu-fifo2 | 4 |  | 3 | 1 |  | 4 | 1 | 4 |  |
| spu-fifo3 | 3 |  | 3 | 1 |  | 3 | 1 | 3 |  |
| spu-health | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| spu-irq | 6 | 7 | 10 | 1 | 6 | 15 | 1 | 10 | 2 |
| spu-kon-touch | 3 |  | 3 | 1 |  | 3 | 1 | 3 |  |
| spu-latch | 6 | 6 | 6 | 1 | 6 | 9 | 1 | 6 | 2 |
| spu-late | 3 |  | 3 | 1 |  | 3 | 1 | 3 |  |
| spu-lead | 6 | 7 | 8 | 1 | 6 | 11 | 1 | 8 | 2 |
| spu-lead-phase | 6 | 6 | 6 | 1 | 6 | 8 | 1 | 5 | 2 |
| spu-lsax-sweep |  |  |  | 6 |  |  |  |  |  |
| spu-path | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| spu-phase | 4 |  | 1 | 1 |  | 6 | 4 | 4 |  |
| spu-phase2 | 4 |  | 5 | 1 |  | 3 | 4 | 4 |  |
| spu-phase3 | 4 |  | 1 | 1 |  | 3 | 4 | 4 |  |
| spu-phase4 |  |  | 4 | 1 |  |  |  | 8 |  |
| spu-phase5 |  |  | 4 | 1 |  |  |  | 8 |  |
| spu-read-grid | 1 | 1 | 1 | 1 | 2 | 4 | 1 | 2 | 2 |
| spu-read-trace | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| spu-rearm | 3 |  | 3 | 1 |  | 3 | 1 | 3 |  |
| spu-regs | 6 | 6 | 8 | 1 | 6 | 11 | 1 | 8 | 2 |
| spu-reverb | 6 | 6 | 16 | 1 | 6 | 15 | 1 | 10 | 2 |
| spu-reverb-in | 6 | 6 | 8 | 1 | 6 | 11 | 1 | 8 | 2 |
| spu-statx-tail |  |  |  | 3 |  |  |  |  |  |
| spu-unk-1dbc | 1 | 6 | 1 | 1 | 6 | 2 | 1 | 2 | 2 |
| spu-unk-kill | 1 | 6 | 1 | 1 | 6 | 2 | 1 | 2 | 2 |
| spu-xfer | 6 | 7 | 8 | 1 | 6 | 12 | 1 | 8 | 2 |
| spu_memory-transfer | 1 | 6 | 3 | 1 | 6 | 7 | 1 | 4 | 2 |
| sr-mask |  |  |  | 3 |  |  |  |  |  |
| st-land | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| store-chain | 1 | 1 | 1 | 1 | 2 | 2 | 1 | 2 | 2 |
| store-cost | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| store-shadow | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| store-stall | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sw-ioread | 3 | 7 | 3 | 1 | 6 | 5 | 1 | 3 | 2 |
| sw-ioread2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sw-ioread3 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sw-ioread4 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sw-sample | 2 | 1 | 3 | 1 | 2 | 6 | 1 | 2 |  |
| t1-edges | 1 | 7 | 1 | 1 | 6 | 2 | 1 | 2 | 2 |
| t1-first-edge | 1 | 1 | 1 | 1 |  | 3 | 1 | 1 |  |
| t1-first-edge2 | 1 | 3 | 1 | 1 |  | 3 | 1 | 1 |  |
| t1-gate | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| t1-hres | 1 | 7 | 1 | 1 | 6 | 2 | 1 | 2 | 2 |
| t1-hsweep | 1 | 7 | 1 | 1 | 6 | 2 | 1 | 2 | 2 |
| t1-line | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| t1-ticks | 1 | 7 | 1 | 1 | 6 | 2 | 1 | 2 | 2 |
| t1-toggle | 6 | 7 | 6 | 1 | 6 | 8 | 1 | 6 | 2 |
| t2-path | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| third-try-stop-500ms |  |  | 1 |  |  |  |  |  |  |
| timer-sync | 6 |  | 10 | 1 | 6 | 14 | 1 | 10 | 2 |
| timer2-lap |  |  |  | 3 |  |  |  |  |  |
| timers_timers | 6 | 6 | 8 | 1 | 6 | 10 | 1 | 8 | 2 |
| unc-entry | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| unc-entry2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| unc-entry3 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| unc-load | 2 | 1 | 2 | 1 | 2 | 6 | 1 | 2 |  |
| unc-load-phase |  |  |  | 1 |  |  |  |  |  |
| unc-store-phase |  |  |  | 1 |  |  |  | 2 |  |
| vblank-edge | 1 | 7 | 3 | 1 | 6 | 6 | 1 | 4 | 2 |
| wb-drain | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| wb-page | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| wb-page2 | 3 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| wr-stream | 1 | 1 | 1 | 1 | 2 | 2 | 1 | 2 | 2 |
| wrong-image-wait2 |  |  | 1 |  |  |  |  |  |  |

Not counted above: logs named without their model, and the programs of [ps1-tests](https://github.com/JaCzekanski/ps1-tests) run on these consoles. Both are in the session folders.
