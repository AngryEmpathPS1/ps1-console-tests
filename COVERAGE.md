# Coverage

How many logs each test has on each console. Generated from the file names in `consoles/`.

| test | SCPH-1000 | SCPH-1001 | SCPH-101 | SCPH-5000 | SCPH-5501 | SCPH-5501/unit-2 | SCPH-7001 | SCPH-7501 | SCPH-7502 | SCPH-9001 | SCPH-9001/borrowed-1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ack-per-device | 1 | 6 | 1 | 7 | 8 | 6 | 6 | 10 | 1 | 8 | 2 |
| at-split | 1 | 3 | 1 | 7 | 3 | 6 | 6 | 5 | 1 | 3 | 2 |
| bios-window | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| biu-dram | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| biu-sched | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| bus-latch | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| card-fetch | 1 |  | 1 |  |  | 3 |  |  |  |  |  |
| card-fetch2 | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| cd-2x-burned |  | 1 |  |  | 1 |  | 2 | 1 | 4 | 1 |  |
| cd-ack-getloc |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-ack-times |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-ack-window |  | 7 | 2 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-adpbusy |  | 1 |  |  | 1 |  | 2 | 1 | 2 | 1 |  |
| cd-boot-read |  | 4 | 2 | 2 | 3 | 3 | 3 | 6 | 2 | 3 |  |
| cd-boot-steps |  | 7 | 2 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-cmds |  | 7 | 2 | 7 | 13 | 3 | 7 | 16 | 2 | 10 | 3 |
| cd-da-capture |  | 1 |  |  | 1 |  | 2 | 1 | 2 | 1 |  |
| cd-firstread |  | 4 | 2 |  |  | 4 |  |  | 4 |  |  |
| cd-firstread2 |  | 4 | 2 |  |  | 4 |  |  | 4 |  |  |
| cd-game-steps |  | 7 | 2 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-gameday |  | 2 |  |  | 2 |  | 2 | 2 | 2 | 2 |  |
| cd-getlocl-seek |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-hc05-tick | 3 | 3 | 2 | 2 | 4 | 3 | 4 | 7 | 4 | 3 |  |
| cd-head |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-hold-after |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-hold-loc |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-hold-locl |  | 4 | 2 | 2 | 4 | 3 | 4 | 7 | 2 | 5 |  |
| cd-init |  | 4 | 1 | 6 | 6 | 2 | 4 | 8 | 4 | 6 | 2 |
| cd-init-after |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-init-args |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-init-far |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-int1-spread |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-irq-glitch | 1 | 1 | 1 | 1 | 2 | 2 | 2 | 4 | 1 | 2 | 2 |
| cd-leadout |  | 4 | 1 | 1 | 4 | 1 | 1 | 5 | 1 | 3 |  |
| cd-lid |  |  |  |  |  |  |  | 3 |  |  |  |
| cd-lid-sweep |  |  |  |  |  |  |  | 3 |  |  |  |
| cd-lid-sweep2 |  |  |  |  |  |  |  | 3 |  |  |  |
| cd-lid-sweep3 |  |  |  |  |  |  | 3 | 3 | 3 | 3 |  |
| cd-lid-sweep4 |  |  |  |  |  |  | 3 |  |  |  |  |
| cd-pastend |  | 2 | 1 | 1 | 3 | 1 | 1 | 4 | 1 | 3 |  |
| cd-pause |  | 11 | 6 | 7 | 12 | 8 | 7 | 17 | 2 | 10 | 3 |
| cd-pausehold |  |  | 2 |  |  | 5 |  | 6 |  |  |  |
| cd-pausewin |  |  | 10 |  |  | 12 |  | 6 |  |  |  |
| cd-playagain |  | 4 | 2 |  |  | 4 |  | 4 | 4 |  |  |
| cd-playagain2 |  | 4 | 2 |  |  | 10 |  | 4 | 4 |  |  |
| cd-queue |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-race |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-ramdump |  | 3 | 2 |  | 3 |  |  | 6 | 3 |  |  |
| cd-readtoc |  | 7 | 2 | 7 | 13 | 3 | 7 | 16 | 2 | 10 | 3 |
| cd-reann |  | 7 | 2 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-scex |  | 11 | 2 | 7 | 12 | 3 | 7 | 17 | 2 | 10 | 3 |
| cd-scex-hold |  | 4 | 2 | 7 | 4 | 3 | 7 | 7 | 2 | 4 | 3 |
| cd-scex-play |  | 2 |  |  | 2 |  | 2 | 2 | 2 | 2 |  |
| cd-seek |  | 4 | 1 | 6 | 6 | 2 | 4 | 8 | 4 | 6 | 2 |
| cd-seek-cancel |  | 3 | 2 |  | 1 |  |  | 6 | 3 |  |  |
| cd-seek-cmd |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-seek-coast |  | 7 | 2 | 2 | 6 | 3 | 6 | 8 | 7 | 6 |  |
| cd-seek-hist |  | 3 | 2 | 2 | 3 | 3 | 6 | 4 | 2 | 3 |  |
| cd-seek-long |  | 3 | 2 | 2 | 3 | 3 | 2 | 7 | 7 | 7 |  |
| cd-seek-map |  | 7 | 2 | 7 | 7 | 3 | 7 | 10 | 2 | 7 | 3 |
| cd-seek-mid |  | 8 | 2 | 2 | 3 | 3 | 7 | 7 | 7 | 7 |  |
| cd-seek-path |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-seek-path2 |  | 8 | 2 | 2 | 7 | 3 | 7 | 7 | 7 | 7 |  |
| cd-seek-path3 |  | 15 | 2 | 8 | 9 | 8 | 8 | 8 | 15 | 8 |  |
| cd-seek-path4 |  | 8 | 2 | 8 | 9 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path5 |  | 8 | 2 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path6 |  | 8 | 2 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path7 |  | 8 | 2 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path8 |  | 8 | 2 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-path9 |  | 8 | 2 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |  |
| cd-seek-pause |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-seek-phase |  | 3 | 2 | 2 | 3 | 3 | 3 | 4 | 2 | 3 |  |
| cd-seek-phase2 |  | 3 | 2 | 2 | 3 | 3 | 3 | 4 | 2 | 3 |  |
| cd-seek-phase3 |  | 3 | 2 | 2 | 3 | 3 | 3 | 4 | 2 | 3 |  |
| cd-sled |  | 3 | 2 | 2 | 3 | 3 | 2 | 7 | 7 | 7 |  |
| cd-small-reads |  | 7 | 2 | 7 | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-speed |  | 4 | 1 | 6 | 6 | 5 | 4 | 8 | 4 | 6 | 2 |
| cd-speedack |  |  | 2 |  |  | 5 |  | 7 |  |  |  |
| cd-speedack2 |  |  | 2 |  |  | 3 |  | 5 |  |  |  |
| cd-speedpause |  | 4 | 1 | 6 | 6 | 2 | 4 | 8 | 4 | 6 | 2 |
| cd-spindown |  |  | 2 |  |  | 8 |  | 8 |  |  |  |
| cd-spindown2 |  |  | 2 |  |  | 4 |  | 4 |  |  |  |
| cd-spinprobe | 2 |  | 2 |  |  | 3 |  |  |  |  |  |
| cd-spinup-phase |  | 3 | 2 | 2 | 3 | 3 | 6 | 7 | 7 | 7 |  |
| cd-spinup-state |  | 3 | 2 | 2 | 3 | 3 | 3 | 7 | 7 | 7 |  |
| cd-spinup-trend |  | 3 | 2 | 3 | 3 | 3 | 3 | 7 | 3 | 3 |  |
| cd-spunup |  | 4 | 1 | 6 | 4 | 2 | 4 | 6 | 1 | 4 | 2 |
| cd-store | 1 | 1 | 1 | 1 | 2 | 2 | 2 | 4 | 1 | 2 | 2 |
| cd-xa |  | 1 |  |  | 2 |  | 2 | 3 | 2 | 3 |  |
| cd-xa-switch |  | 1 |  |  | 1 |  | 2 | 3 | 2 | 2 |  |
| cd-xa-switch2x |  | 7 | 2 |  | 10 | 3 | 7 | 13 | 2 | 10 | 3 |
| cd-xa-switch2x-ff7 |  |  |  | 6 |  |  |  |  |  |  |  |
| cd-xa-switch2x-ff7eu |  |  |  |  |  |  |  |  | 2 |  |  |
| cdrom_getloc |  | 1 | 1 |  | 2 | 1 | 1 | 2 |  | 2 |  |
| cdrom_timing |  | 7 | 2 | 4 | 9 | 2 | 7 | 13 | 2 | 10 | 3 |
| clut-cost | 1 | 6 | 1 | 7 | 10 | 3 | 6 | 12 | 1 | 8 | 2 |
| code-in-io-irqsplit | 1 |  |  |  |  | 3 |  |  |  |  |  |
| code-in-io-prime | 1 |  |  |  |  | 3 |  |  |  |  |  |
| code-in-io-rest | 1 |  |  |  |  | 3 |  |  |  |  |  |
| code-in-io-safe |  | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| cold-bracket | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| cold-branch | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| cold-window | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| concurrent-dma | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| counter-bracket | 1 | 1 | 1 | 7 | 5 | 6 | 6 | 10 | 1 | 6 | 2 |
| counter-store | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| cpu-code-in-irq |  | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| cpu-extras | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| cpu-loop-tick | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick10 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick11 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick12 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick13 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick14 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick15 | 1 | 2 | 1 |  | 3 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick16 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick17 | 1 | 2 | 1 |  |  | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick18 | 1 | 2 | 1 |  |  | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick2 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick3 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick4 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick5 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick6 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick7 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick8 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu-loop-tick9 | 1 | 2 | 1 |  | 2 | 1 |  | 2 | 1 | 2 |  |
| cpu_access-time | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| cpu_code-in-io | 1 | 1 |  | 1 | 2 |  | 1 | 2 | 1 | 2 |  |
| cpu_cop | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| dma-arb | 1 | 6 | 1 | 6 | 6 | 3 | 6 | 8 | 1 | 5 | 2 |
| dma-chopx | 1 | 2 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| dma-contention | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| dma-disp | 1 | 2 | 1 |  | 2 | 3 |  | 4 | 2 |  |  |
| dma-len | 1 | 2 | 1 |  | 5 | 2 |  | 4 | 2 |  |  |
| dma-len0 | 1 | 2 | 1 |  | 4 | 3 |  | 4 | 2 |  |  |
| dma-loss | 1 | 2 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| dma-loss2 | 1 | 2 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| dma-order | 1 | 2 | 1 |  | 14 | 3 |  | 4 | 2 |  |  |
| dma-rates | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 11 | 1 | 7 | 1 |
| dma-rows | 1 | 2 | 1 |  | 5 | 3 |  | 4 | 2 |  |  |
| dma-setup | 1 | 2 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| dma-trace | 1 | 2 | 1 |  | 3 | 3 |  | 4 | 2 |  |  |
| dma_chopping | 1 | 6 | 1 | 6 | 11 | 3 | 6 | 14 | 1 | 10 | 2 |
| dma_dpcr | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| dma_otc-test | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| draw-area | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| fifo-burst | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| fill-gap | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| first-try-app-running |  |  |  |  | 1 |  |  |  |  |  |  |
| first-try-asleep |  |  |  |  | 1 |  |  |  |  |  |  |
| fix-shadow | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| fix-shadow2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| fix-shadow3 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| fourth-try-stop-polled |  |  |  |  | 1 |  |  |  |  |  |  |
| fuzz-bus | 1 | 2 | 6 |  | 2 | 3 |  | 4 | 2 |  |  |
| fuzz-cpu | 1 | 3 | 6 |  | 3 | 3 |  | 2 | 3 |  |  |
| fuzz-dma | 1 | 2 | 6 |  | 3 | 2 |  | 4 | 2 |  |  |
| fuzz-gpu | 1 | 1 | 6 |  | 3 | 3 |  | 4 | 2 |  |  |
| fuzz-gte | 1 | 1 | 6 |  | 1 | 6 |  | 4 | 2 |  |  |
| fuzz-mdec | 1 | 1 | 6 |  | 8 | 3 |  | 4 | 2 |  |  |
| fuzz-spu | 1 | 1 | 6 |  | 7 | 3 |  | 4 | 2 |  |  |
| fuzz-trail | 1 | 2 | 6 |  | 2 | 3 |  | 2 | 2 |  |  |
| gp0-bit25 | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gp0-bit25b | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gp0-bit25c | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| gp0-overrun | 1 | 2 | 1 |  |  | 6 |  | 4 | 1 |  |  |
| gp0-poll | 1 | 2 | 1 | 7 | 2 | 5 | 6 | 4 | 1 | 2 |  |
| gp0-store | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| gp1-reset | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gp1-reset-480i | 1 |  | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0-ready | 1 | 6 | 1 | 6 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| gpu-a0-size | 1 | 1 | 1 | 7 | 7 | 3 | 6 | 6 | 1 | 4 | 2 |
| gpu-a0after | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0blk | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0blk2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0blk3 | 1 | 2 | 1 |  |  | 5 |  | 4 | 2 |  |  |
| gpu-a0chop | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0fill | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0lsf | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0mode | 1 | 2 | 1 |  |  | 6 |  | 4 | 2 |  |  |
| gpu-a0pace | 1 | 4 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| gpu-a0phase | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll10 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll11 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll3 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll4 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll5 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll6 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll7 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0poll9 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0pollbs | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-a0rate | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0rate2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-a0row | 1 | 4 | 1 |  |  | 8 |  | 4 | 2 |  |  |
| gpu-a0rowdisp | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0stall | 1 | 3 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-a0vph | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-abort | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| gpu-batch | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-batch2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-batch3 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-burstline | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-burstq | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-burstq2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-burstq3 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-c0-cold | 1 | 3 | 1 | 7 | 6 | 3 | 6 | 5 | 1 | 3 | 2 |
| gpu-c0-cold2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| gpu-c0-datel | 1 | 6 | 1 | 6 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| gpu-c0-dir3 | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-c0-gaps | 1 | 6 | 1 | 6 | 6 | 3 | 6 | 6 | 1 | 4 | 2 |
| gpu-c0-ready | 1 | 1 | 1 | 7 | 6 | 3 | 6 | 6 | 1 | 4 | 2 |
| gpu-c0-size | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 | 2 |
| gpu-clut-dma | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-clut-ready | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-clut-ready2 | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-clut-ready3 | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-clut-ready4 | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-cmd-ready | 1 | 1 | 1 | 6 | 7 | 3 | 6 | 4 | 1 | 2 | 2 |
| gpu-copy-480i | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-copy2 | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-copy3 | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-copy4 | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-cost-sweep | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 13 | 1 | 8 | 2 |
| gpu-degen | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-degen2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-dma-blkper | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-gaps | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-gaps2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-gaps3 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-gapsd | 1 | 6 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-gapsd2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-hold | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-ifetch | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-iorun | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-knee | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-lasttie | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-pace | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-pair | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-progress | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-progress2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-quiet | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-ramrow | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rdbs | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rdlen | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rdlong | 1 |  | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-rdloop | 1 |  | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-rdloop2 | 1 |  | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-rdpoll | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rdpoll2 | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-refctl | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-refresh | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| gpu-dma-rfpace | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-rfpace2 | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rowburst | 1 |  | 1 |  |  | 2 |  | 2 | 2 |  |  |
| gpu-dma-rowdown | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rowend | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rowlen | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rowlen2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-dma-rows | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rows2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-rowx | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-size | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| gpu-dma-span | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-stoiter | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-stoiter2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-stolast | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-stolast2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-stolast3 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-stolast4 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-stoll | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| gpu-dma-storow | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-storun | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dma-turn | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-dmabs | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-dmabs2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-dmastream | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-dmastream2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-dmastream3 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-dmastream4 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-dmastream5 | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-dreq-fill | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| gpu-drift | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-fill-480i | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-gp1scan | 1 | 2 | 1 |  | 2 | 3 |  | 4 | 2 |  |  |
| gpu-height | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 6 | 1 | 6 |  |
| gpu-irq-chain | 1 | 3 | 1 | 7 | 6 | 3 | 6 | 5 | 1 | 3 | 2 |
| gpu-lines | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-lines2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-ll-ahead | 1 |  | 1 |  |  | 4 |  | 4 | 2 |  |  |
| gpu-ll-end | 1 | 2 | 1 |  |  | 3 |  | 2 | 2 |  |  |
| gpu-ll-start | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| gpu-ll-stat | 1 |  | 1 |  |  | 2 |  | 4 | 2 |  |  |
| gpu-outside | 1 | 2 | 1 |  |  | 5 |  | 4 | 1 |  |  |
| gpu-outside2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-outside3 | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-outside4 | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-outside5 | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-phase | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-phase2 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-rect-cached | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-rect-gap | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-rect-x | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| gpu-rect-x2 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| gpu-revive | 1 | 2 | 1 |  | 1 |  |  | 2 | 2 |  |  |
| gpu-scan3 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-scan4 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-scan5 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-scan6 | 1 | 2 | 1 | 7 | 6 | 4 | 6 | 4 | 1 | 6 |  |
| gpu-scanline | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-scanline2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-scanline3 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-scanout | 1 | 3 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 3 | 2 |
| gpu-scanout2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 9 | 1 | 6 |  |
| gpu-semi-shape | 1 | 3 | 1 | 7 | 6 | 3 | 6 | 5 | 1 | 3 | 2 |
| gpu-shape-time | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time10 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time11 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time12 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time13 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time14 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time15 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time16 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time17 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-shape-time18 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time19 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time2 | 1 | 2 | 1 | 7 | 7 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time20 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time21 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time22 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time23 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time24 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time25 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time26 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time27 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time28 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time29 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time3 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time30 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time31 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time32 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time33 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time34 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time35 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time36 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time37 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time38 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time39 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time4 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time40 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time41 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time42 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time43 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time44 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time45 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time46 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time47 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time48 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time49 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time5 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time50 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time51 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time52 | 1 | 2 | 1 | 6 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time53 | 1 | 2 | 1 |  | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time54 | 1 | 2 | 1 | 6 | 2 | 5 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time55 | 1 | 2 | 1 | 6 | 2 | 5 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time56 | 1 | 2 | 1 | 1 | 2 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time57 | 1 | 2 | 1 | 1 | 2 | 3 | 2 | 4 | 1 | 2 |  |
| gpu-shape-time58 | 1 | 2 | 1 | 1 | 2 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time59 | 1 | 2 | 1 | 1 | 2 | 3 | 2 | 4 | 1 | 2 |  |
| gpu-shape-time6 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time60 | 1 | 2 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time61 | 1 | 2 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time62 | 1 | 2 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time63 | 1 | 2 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time64 | 1 | 2 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time65 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time66 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time67 | 1 | 1 | 1 |  | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time68 | 1 | 1 | 1 | 1 | 1 | 3 |  | 6 | 1 | 1 |  |
| gpu-shape-time68c | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time68e | 1 | 1 | 1 | 1 | 1 | 3 |  | 12 | 1 | 1 |  |
| gpu-shape-time69 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time7 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time70 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time71 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time72 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time73 | 1 | 3 | 1 | 1 | 1 | 1 |  | 3 | 1 | 1 |  |
| gpu-shape-time74 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time75 | 1 | 1 | 1 |  | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time76 | 1 | 1 | 1 | 1 | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time77 | 1 | 1 | 1 | 1 | 1 | 3 |  | 3 | 1 | 1 |  |
| gpu-shape-time78 | 1 | 1 | 1 |  | 1 | 3 |  | 4 | 1 | 1 |  |
| gpu-shape-time8 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-shape-time9 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-tail | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 3 | 1 | 6 |  |
| gpu-tail2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-tail3 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-tail4 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-tail5 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-tail6 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-tail7 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 6 |  |
| gpu-tail8 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-tail9 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-tcset | 1 | 2 | 1 |  | 1 |  |  | 2 | 2 |  |  |
| gpu-tcwords | 1 | 2 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| gpu-tex-fill1 |  |  |  |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-fill2 |  |  |  |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-fill3 |  |  |  |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-fill4 |  |  |  |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-fill5 |  |  |  |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-fill6 |  |  |  |  |  | 3 |  |  |  |  |  |
| gpu-tex-gap |  |  |  |  |  | 3 |  |  |  |  |  |
| gpu-tex-ilace |  |  |  |  |  | 3 |  |  |  |  |  |
| gpu-tex-maskpart |  |  |  |  |  | 3 |  |  |  |  |  |
| gpu-tex-noscan | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-opaque | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| gpu-tex-part |  |  |  |  |  | 3 |  |  |  |  |  |
| gpu-tex-part2 |  |  |  |  |  | 3 |  |  |  |  |  |
| gpu-tex-scale | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-semipart |  |  |  |  |  | 3 |  |  |  |  |  |
| gpu-tex-stride | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-stride2 | 1 |  | 1 |  |  | 3 |  | 3 |  |  |  |
| gpu-tex-stride3 |  |  |  |  |  | 3 |  | 3 |  |  |  |
| gpu-thin | 1 | 3 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 3 | 2 |
| gpu-timectl | 1 | 2 | 1 |  | 2 | 3 |  | 4 | 2 |  |  |
| gpu-transp-cached | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| gpu-tri-ready | 1 | 3 | 1 | 7 | 6 | 3 | 6 | 5 | 1 | 3 | 2 |
| gpu-vbl | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-vbl2 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-vbl3 | 1 | 2 | 1 | 7 | 6 | 3 | 6 | 4 | 1 | 2 |  |
| gpu-version | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| gpu-wh | 1 | 2 | 1 |  | 3 | 3 |  | 4 | 2 |  |  |
| gpu-whv | 1 | 2 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| gpu-whv2 | 1 | 2 | 1 |  | 1 | 3 |  | 4 | 2 |  |  |
| gpu_bandwidth | 1 | 6 | 1 | 6 | 9 | 4 | 6 | 10 | 1 | 8 | 2 |
| gpu_gp0-e1 | 1 | 1 | 1 | 6 | 6 | 3 | 6 | 6 | 1 | 4 | 2 |
| gpu_mask-bit | 1 | 1 | 1 | 6 | 6 | 3 | 6 | 6 | 1 | 4 | 2 |
| gpuinfo-latch | 1 | 1 | 1 | 7 | 6 | 3 | 6 | 6 | 1 | 4 | 2 |
| gpustat | 1 | 1 | 1 | 6 | 6 | 3 | 6 | 6 | 1 | 4 | 2 |
| gpustat31 | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| gte-hold | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| gte-irgb | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| gte-latch-cached | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| gte-sqr-align | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| gte-undef | 1 | 3 | 1 |  | 2 | 3 |  | 4 | 2 |  |  |
| gte-write-delay | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| gte_test-all | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| hold-read | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| icache-tagmatch | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| icache-valid | 1 | 2 | 1 |  | 1 |  |  | 2 | 2 |  |  |
| imask-upper | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| io-chain | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| io-pair2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| io-pairs | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| io-ram | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| io-shadow | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 4 | 1 | 2 | 2 |
| io-store-shadow | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| irq-unmask | 1 |  | 1 |  |  | 3 |  | 2 | 2 |  |  |
| irq-unmask2 | 1 |  | 1 |  |  | 2 |  | 2 | 2 |  |  |
| irq-unmask3 | 1 |  | 1 |  |  | 2 |  | 2 | 2 |  |  |
| line-fill | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| line-fill2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| line-split | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| load-fill | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| load-fill2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill3 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill4 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill5 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-fill6 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| load-pairs | 1 | 1 | 1 | 6 | 5 | 6 | 6 | 12 | 1 | 6 | 2 |
| load-use | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| loop-cold | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| machine-id | 1 | 9 | 1 | 7 | 27 | 3 | 6 | 16 | 1 | 13 | 2 |
| mdec-color | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| mdec-glitch24 | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| mdec-idct | 2 | 6 | 1 | 6 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| mdec-idct2 | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 6 | 1 | 2 |  |
| mdec-in-hang | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| mdec-leftover | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mdec-mono-out | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mdec-pad | 1 | 2 | 1 |  |  | 3 |  | 4 | 5 |  |  |
| mdec-pad2 | 1 | 2 | 1 |  |  | 1 |  | 2 | 2 |  |  |
| mdec-padding | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mdec-qbias | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mdec-reset | 1 | 1 | 1 | 6 | 3 | 15 | 6 | 6 | 1 | 4 | 2 |
| mdec-reset-align | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset-grid | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset-grid2 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset-grid3 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset-half | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset-tail | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset-tailgrid | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset10 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset11 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset12 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset13 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset14 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset15 | 1 | 2 | 1 |  |  | 4 |  | 4 | 2 |  |  |
| mdec-reset16 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset17 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset18 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset19 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset2 | 1 | 2 | 1 |  | 1 | 3 |  | 2 | 2 |  |  |
| mdec-reset20 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset21 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset22 | 1 | 2 | 1 |  |  | 4 |  | 4 | 2 |  |  |
| mdec-reset3 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| mdec-reset4 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset5 | 1 | 2 | 1 |  |  | 2 |  | 2 | 1 |  |  |
| mdec-reset6 | 1 | 2 | 1 |  |  | 2 |  | 2 | 1 |  |  |
| mdec-reset7 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-reset8 | 1 | 2 | 1 |  |  | 2 |  | 2 | 1 |  |  |
| mdec-reset9 | 1 | 2 | 1 |  |  | 2 |  | 2 | 1 |  |  |
| mdec-round | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mdec-scale | 1 | 2 | 1 | 7 | 2 | 6 | 6 | 6 | 1 | 2 |  |
| mdec-scale2 | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| mdec-step24 | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mdec-tail | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mdec-time | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mdec-winddown | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| mem-delay | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| mem-low | 1 |  | 1 |  |  | 1 |  | 2 | 2 |  |  |
| miss-grid | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| movie-browser-depths |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-browser-fixed |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-browser-hang-probe |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-browser-hangs |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-browser-speed |  |  |  |  | 1 |  |  |  |  |  |  |
| movie-browser-timing |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-browser-xfiles |  |  |  |  | 1 |  |  |  |  |  |  |
| movie-disc-box1 |  |  |  |  | 1 |  |  |  | 1 |  |  |
| movie-disc-box2 |  |  |  |  | 1 |  |  |  | 1 |  |  |
| movie-disc-extras |  |  |  |  | 1 |  |  |  | 1 |  |  |
| movie-disc-final |  |  |  | 1 | 1 |  |  |  | 1 |  |  |
| movie-disc-hunt1 |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-disc-hunt2 |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-disc-hunt3 |  |  |  |  |  |  |  |  | 1 |  |  |
| movie-disc-hunt4 |  |  |  |  |  |  |  |  | 1 |  |  |
| mul-grid | 1 | 3 | 1 |  | 3 |  |  | 2 | 2 |  |  |
| mul-shadow | 1 | 3 | 1 |  | 3 |  |  | 2 | 2 |  |  |
| muldiv-overlap | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| multitap-modes |  |  |  | 1 |  |  |  |  |  |  |  |
| pad-ack-hist | 1 | 6 | 1 | 6 | 6 | 6 | 6 | 8 | 1 | 6 | 2 |
| pad-config |  |  |  |  |  |  |  |  |  | 3 |  |
| pad-short-exit |  |  |  |  | 1 |  |  |  |  |  |  |
| pair-atlas | 1 | 2 | 1 |  | 5 | 3 |  | 4 | 2 |  |  |
| pair-cdep | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pair-cext | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pair-cold | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pair-dep | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pair-ext | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pair-mid | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pair-unc | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pair-warm | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| pal-dots | 1 | 6 | 1 |  | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| pal-line | 1 | 6 | 1 |  | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| pal-probe |  |  |  | 1 |  |  |  |  |  |  |  |
| pal-probe2 |  |  |  | 1 |  |  |  |  |  |  |  |
| pocket-cmds |  |  |  |  | 6 |  |  | 1 |  |  |  |
| pocket-protect |  |  |  |  | 1 |  |  |  |  |  |  |
| pocket-start |  |  |  |  | 1 |  |  |  |  |  |  |
| pocket-write |  |  |  |  | 2 |  |  |  |  |  |  |
| ram-banks | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| ram-store-io | 1 | 2 | 1 | 7 | 2 | 6 | 6 | 4 | 1 | 2 |  |
| ram-store-io2 | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| ram-store-io3 | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 2 | 1 | 2 |  |
| ram-store-io4 | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| ram-store-io5 | 1 | 2 | 1 | 7 | 2 | 6 | 6 | 4 | 1 | 2 |  |
| ram-store-io6 | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| ram-store-io7 | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 1 | 1 | 2 |  |
| ram-store-io8 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| redux-spu | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| redux-spuenvx | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| redux-spuenvxd12 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| redux-spuenvxd16 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| redux-spuenvxd20 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| redux-spuenvxd24 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| redux-spuenvxd28 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| redux-spuenvxd32 | 1 | 2 | 1 |  |  | 1 |  | 2 | 2 |  |  |
| redux-spuenvxd36 | 1 | 2 | 1 |  |  | 1 |  | 2 | 2 |  |  |
| redux-spuenvxd4 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| redux-spuenvxd40 | 1 | 2 | 1 |  |  | 1 |  | 2 | 2 |  |  |
| redux-spuenvxd44 | 1 | 2 | 1 |  |  | 1 |  | 2 | 2 |  |  |
| redux-spuenvxd48 | 1 | 2 | 1 |  |  | 2 |  | 2 | 2 |  |  |
| redux-spuenvxd8 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| refresh-cold | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-io | 1 | 3 | 1 | 7 | 3 | 6 | 6 | 5 | 1 | 3 | 2 |
| refresh-io2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-io3 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-io4 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| refresh-pair | 1 | 2 | 1 |  |  | 3 |  | 2 | 3 |  |  |
| refresh-store | 1 | 2 | 1 |  | 3 | 1 |  | 2 | 1 | 2 |  |
| refresh-store2 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| refresh-store3 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| refresh-store4 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| refresh-store5 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| refresh-store6 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| refresh-store7 | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| refresh-warm | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| second-try-after-stop |  |  |  |  | 1 |  |  |  |  |  |  |
| semi-cost | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| seq-miss | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| seq-miss2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| shadow-miss | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| shadow-miss2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sio1-loopback |  |  |  |  |  |  |  |  |  | 8 |  |
| sio1-mismatch |  |  |  |  |  |  |  | 1 |  |  |  |
| slow-b2b | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| spu-busy | 1 | 6 | 1 | 6 | 6 | 3 | 6 | 9 | 1 | 6 | 2 |
| spu-curvol | 1 | 1 | 1 | 6 | 2 | 6 | 6 | 6 | 1 | 2 | 2 |
| spu-decode-ahead | 1 | 6 | 1 | 6 | 6 | 3 | 6 | 10 | 1 | 6 | 2 |
| spu-dma | 1 | 6 | 1 | 6 | 8 | 3 | 6 | 11 | 1 | 7 | 1 |
| spu-dmaread | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| spu-dmaread2 | 1 | 2 | 1 |  |  | 2 |  | 4 | 2 |  |  |
| spu-endcut | 1 | 1 | 1 |  | 4 | 3 |  | 4 | 2 |  |  |
| spu-envphase-move | 1 |  | 1 |  |  | 3 |  | 4 |  |  |  |
| spu-envphase-move2 | 1 |  | 1 |  |  | 3 |  | 4 |  |  |  |
| spu-envring | 1 | 1 | 1 |  | 2 | 2 |  | 4 | 2 |  |  |
| spu-exp-neg | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| spu-fifo | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| spu-fifo2 | 1 | 4 | 1 |  | 3 | 3 |  | 4 | 1 | 4 |  |
| spu-fifo3 | 1 | 3 | 1 |  | 3 | 3 |  | 3 | 1 | 3 |  |
| spu-health | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| spu-irq | 1 | 6 | 1 | 7 | 10 | 3 | 6 | 15 | 1 | 10 | 2 |
| spu-kon-touch | 1 | 3 | 1 |  | 3 | 3 |  | 3 | 1 | 3 |  |
| spu-latch | 1 | 6 | 1 | 6 | 6 | 3 | 6 | 9 | 1 | 6 | 2 |
| spu-late | 1 | 3 | 1 |  | 3 | 3 |  | 3 | 1 | 3 |  |
| spu-lead | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 11 | 1 | 8 | 2 |
| spu-lead-phase | 1 | 6 | 1 | 6 | 6 | 3 | 6 | 8 | 1 | 5 | 2 |
| spu-lsax-sweep | 1 | 2 | 1 |  |  | 6 |  | 4 | 1 |  |  |
| spu-noise | 1 | 3 | 1 |  | 3 | 3 |  | 4 | 3 |  |  |
| spu-path | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| spu-phase | 1 | 4 | 1 |  | 1 | 3 |  | 6 | 4 | 4 |  |
| spu-phase2 | 1 | 4 | 1 |  | 5 | 3 |  | 3 | 4 | 4 |  |
| spu-phase3 | 1 | 4 | 1 |  | 1 | 3 |  | 3 | 4 | 4 |  |
| spu-phase4 | 1 | 2 | 1 |  | 4 | 3 |  | 4 | 1 | 8 |  |
| spu-phase5 | 1 | 2 | 1 |  | 4 | 3 |  | 4 | 1 | 8 |  |
| spu-pmon | 1 | 3 | 1 |  | 3 | 3 |  | 4 | 3 |  |  |
| spu-read-grid | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 4 | 1 | 2 | 2 |
| spu-read-trace | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| spu-rearm | 1 | 3 | 1 |  | 3 | 3 |  | 3 | 1 | 3 |  |
| spu-regs | 1 | 6 | 1 | 6 | 8 | 3 | 6 | 11 | 1 | 8 | 2 |
| spu-reverb | 1 | 6 | 1 | 6 | 16 | 3 | 6 | 15 | 1 | 10 | 2 |
| spu-reverb-in | 1 | 6 | 1 | 6 | 8 | 3 | 6 | 11 | 1 | 8 | 2 |
| spu-statx-tail | 1 | 2 | 1 |  |  | 3 |  | 4 | 1 |  |  |
| spu-unk-1dbc | 1 | 1 | 1 | 6 | 1 | 6 | 6 | 4 | 1 | 2 | 2 |
| spu-unk-kill | 1 | 1 | 1 | 6 | 1 | 6 | 6 | 4 | 1 | 2 | 2 |
| spu-xfer | 1 | 6 | 1 | 7 | 8 | 3 | 6 | 12 | 1 | 8 | 2 |
| spu_memory-transfer | 1 | 1 | 1 | 6 | 3 | 6 | 6 | 7 | 1 | 4 | 2 |
| sr-mask | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| st-land | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| state-peek | 1 | 2 | 1 |  | 2 | 3 |  | 4 | 2 |  |  |
| store-chain | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 2 | 1 | 2 | 2 |
| store-cost | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| store-shadow | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| store-stall | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| store-stream | 1 | 2 | 1 |  |  | 1 |  | 2 | 1 |  |  |
| sw-ioread | 1 | 3 | 1 | 7 | 3 | 6 | 6 | 5 | 1 | 3 | 2 |
| sw-ioread2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sw-ioread3 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sw-ioread4 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| sw-sample | 1 | 2 | 1 | 1 | 3 | 1 | 2 | 6 | 1 | 2 |  |
| t0-dot-modewrite | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| t0-dot-write | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| t0-dot-write2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| t1-count-edge | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| t1-count-edge2 | 1 | 2 | 1 |  |  | 3 |  | 4 | 2 |  |  |
| t1-edges | 1 | 1 | 1 | 7 | 1 | 6 | 6 | 4 | 1 | 2 | 2 |
| t1-first-edge | 1 | 1 | 1 | 1 | 1 | 1 |  | 3 | 1 | 1 |  |
| t1-first-edge2 | 1 | 1 | 1 | 3 | 1 | 3 |  | 3 | 1 | 1 |  |
| t1-gate | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| t1-hres | 1 | 1 | 1 | 7 | 1 | 6 | 6 | 4 | 1 | 2 | 2 |
| t1-hsweep | 1 | 1 | 1 | 7 | 1 | 6 | 6 | 4 | 1 | 2 | 2 |
| t1-line | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| t1-ticks | 1 | 1 | 1 | 7 | 1 | 6 | 6 | 4 | 1 | 2 | 2 |
| t1-toggle | 1 | 6 | 1 | 7 | 6 | 3 | 6 | 8 | 1 | 6 | 2 |
| t2-path | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| third-try-stop-500ms |  |  |  |  | 1 |  |  |  |  |  |  |
| timer-sync | 1 | 6 | 1 |  | 10 | 3 | 6 | 14 | 1 | 10 | 2 |
| timer2-lap | 1 | 2 | 1 |  |  | 3 |  | 2 | 1 |  |  |
| timers_timers | 1 | 6 | 1 | 6 | 8 | 3 | 6 | 10 | 1 | 8 | 2 |
| tri-cold | 1 | 1 | 1 |  | 2 |  |  | 2 | 2 |  |  |
| unc-entry | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| unc-entry2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| unc-entry3 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| unc-load | 1 | 2 | 1 | 1 | 2 | 1 | 2 | 6 | 1 | 2 |  |
| unc-load-phase | 1 | 2 | 1 |  |  | 1 |  | 2 | 1 |  |  |
| unc-store-phase | 1 | 2 | 1 |  |  | 1 |  | 2 | 1 | 2 |  |
| vblank-edge | 1 | 1 | 1 | 7 | 3 | 6 | 6 | 6 | 1 | 4 | 2 |
| wb-drain | 1 | 1 | 1 | 1 | 2 | 1 | 2 | 4 | 1 | 2 | 2 |
| wb-page | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| wb-page2 | 1 | 3 | 1 | 1 | 3 | 1 | 2 | 5 | 1 | 3 | 2 |
| wr-stream | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 2 | 1 | 2 | 2 |
| wrong-image-wait2 |  |  |  |  | 1 |  |  |  |  |  |  |

Not counted above: logs named without their model, and the programs of [ps1-tests](https://github.com/JaCzekanski/ps1-tests) run on these consoles. Both are in the session folders.
