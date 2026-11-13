# hwtests/fuzz-all/wrap.s - the timed wrapper of the whole-console fuzzer (main.cpp says what it is for).
#
# Everything that runs between a case's cold run and its warm run is in this one 4 KB-aligned block, so it occupies
# I-cache indices 0..(fz_wrap_end - fz_wrap_start) and nothing else: the case's code is placed above that index range
# (main.cpp codeOff), and its lines are still cached when the warm run starts (hwtests/pair-warm's trap: C code between
# trials shares the case's index and leaves it cold).
#
# THE ALIGNMENT. Each timed run starts at the same place on the DRAM refresh's 256-cycle grid and the SPU's 1536-cycle
# sample pair, whatever ran before: Timer 2 (wrapping at 49152, main.cpp) is read, r = (T2 - e) mod 1536 (e = Timer 2
# just after main set it up), and 1535 - r cycles are burned in cached code (fz_timed), so the start is e + K
# (mod 1536) for a constant K. A case that runs long or short on one machine then does not move every case after it:
# each case is its own experiment (three copies of a control case per batch check it: tools/fuzzloop.js).
#
# Registers in a case (the generator, tools/fuzzall.js, keeps to this):
#   s0 = DATA (KSEG0), s3 = DATA (KSEG1), s1 = 1F801000h (I/O), s2 = 1F800000h (scratchpad) - bases, never written
#   fp = Timer 2 at the start, gp = the case's address, k0 k1 ra sp - the wrapper's
#   at v0 v1 a0-a3 t0-t9 s4-s7 - the case's own, loaded from the context before the run, saved after it

        .set noreorder
        .set noat

        .equ CTX,      0x80103400
        .equ DATA,     0x80102000
        .equ DATAINIT, 0x80102800
        .equ SCRINIT,  0x80103000
        .equ OUT0,     0x80104000
        .equ OUT1,     0x80105000

        # register n+1 of an unrolled pair (GAS writes a register as a literal, so the sum goes through altmacro's %())
        .altmacro
        .macro fz_ctc2_r m
        ctc2  $t1, $\m
        .endm
        .macro fz_ctc2_next n
        fz_ctc2_r %(\n+1)
        .endm
        .macro fz_cfc2_r m
        cfc2  $t0, $\m
        .endm
        .macro fz_cfc2_next n
        fz_cfc2_r %(\n+1)
        .endm

        .section .text.fzwrap,"ax",@progbits
        .balign 4096
        .global fz_wrap_start
fz_wrap_start:

# void fz_pair(void): the cold run (OUT0) and the warm run (OUT1) of the case CTX describes, back to back
        .global fz_pair
fz_pair:
        addiu $sp, $sp, -64
        sw    $ra, 60($sp)
        sw    $s0, 56($sp)
        sw    $s1, 52($sp)
        sw    $s2, 48($sp)
        sw    $s3, 44($sp)
        sw    $s4, 40($sp)
        sw    $s5, 36($sp)
        sw    $s6, 32($sp)
        sw    $s7, 28($sp)
        sw    $fp, 24($sp)
        sw    $gp, 20($sp)
        lui   $t0, %hi(OUT0)
        addiu $t0, $t0, %lo(OUT0)
        bal   fz_one
        sw    $t0, 12($sp)
        lui   $t0, %hi(OUT1)
        addiu $t0, $t0, %lo(OUT1)
        bal   fz_one
        sw    $t0, 12($sp)
        lw    $ra, 60($sp)
        lw    $s0, 56($sp)
        lw    $s1, 52($sp)
        lw    $s2, 48($sp)
        lw    $s3, 44($sp)
        lw    $s4, 40($sp)
        lw    $s5, 36($sp)
        lw    $s6, 32($sp)
        lw    $s7, 28($sp)
        lw    $fp, 24($sp)
        lw    $gp, 20($sp)
        jr    $ra
        addiu $sp, $sp, 64

# one run: restore, warm the wrapper with a dummy case, align, time the case, save everything
# frame slots (fz_pair's): 0 e, 4 the case's address, 8 fz_timed's ra, 12 the output, 16 fz_one's ra
fz_one:
        sw    $ra, 16($sp)
        # DATA (2 KB) from its initial copy
        lui   $t0, %hi(DATAINIT)
        addiu $t0, $t0, %lo(DATAINIT)
        lui   $t1, %hi(DATA)
        addiu $t1, $t1, %lo(DATA)
        li    $t2, 512
1:      lw    $t3, 0($t0)
        addiu $t2, $t2, -1
        sw    $t3, 0($t1)
        addiu $t0, $t0, 4
        bnez  $t2, 1b
        addiu $t1, $t1, 4
        # the scratchpad (1 KB)
        lui   $t0, %hi(SCRINIT)
        addiu $t0, $t0, %lo(SCRINIT)
        lui   $t1, 0x1F80
        li    $t2, 256
2:      lw    $t3, 0($t0)
        addiu $t2, $t2, -1
        sw    $t3, 0($t1)
        addiu $t0, $t0, 4
        bnez  $t2, 2b
        addiu $t1, $t1, 4
        # I/O state every run starts from
        lui   $s1, 0x1F80
        ori   $s1, $s1, 0x1000
        lui   $a0, %hi(CTX)
        addiu $a0, $a0, %lo(CTX)
        lui   $t0, 0x8000
        sw    $t0, 0x824($s1)          # MDEC reset
        lui   $t0, 0x0100
        sw    $t0, 0x814($s1)          # GP1(01h): the command buffer reset
        sw    $t0, 0x810($s1)          # GP0(01h): the texture cache cleared
        lui   $t0, 0x0400
        sw    $t0, 0x814($s1)          # GP1(04h) 0: no DMA direction

        sw    $zero, 0x074($s1)        # I_MASK
        sw    $zero, 0x070($s1)        # I_STAT acknowledged
        sw    $zero, 0x104($s1)        # Timer 0 mode (and its count reset)
        sw    $zero, 0x114($s1)        # Timer 1 mode
        sw    $zero, 0x108($s1)        # Timer 0 target
        sw    $zero, 0x118($s1)        # Timer 1 target
        lw    $t0, 448($a0)            # DPCR, DICR as main found them (ctx 112, 113)
        lw    $t1, 452($a0)
        sw    $t0, 0x0F0($s1)
        sw    $t1, 0x0F4($s1)
        # the GPU idle again (GPUSTAT bit 26), bounded
        li    $t2, 100000
3:      lw    $t0, 0x814($s1)
        addiu $t2, $t2, -1
        srl   $t0, $t0, 26
        andi  $t0, $t0, 1
        bnez  $t0, 4f
        nop
        bnez  $t2, 3b
        nop
4:
        # GPUREAD latched on one fixed word: a C0h read of the texture region's first four pixels (constant for the batch)
        lui   $t0, 0xC000
        sw    $t0, 0x810($s1)
        lui   $t0, 0x0100
        ori   $t0, $t0, 0x0080
        sw    $t0, 0x810($s1)          # (128,256)
        lui   $t0, 0x0001
        ori   $t0, $t0, 0x0004
        sw    $t0, 0x810($s1)          # 4 x 1: two words (after one word GPUREAD keeps what it held before - src/psx.js M223, amidog)
        li    $t2, 100000
9:      lw    $t0, 0x814($s1)
        addiu $t2, $t2, -1
        srl   $t0, $t0, 27
        andi  $t0, $t0, 1
        bnez  $t0, 10f
        nop
        bnez  $t2, 9b
        nop
10:     lw    $t0, 0x810($s1)
        nop
        lw    $t0, 0x810($s1)
        # the GTE: control registers, then data registers, from the context
        .irp n,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30
        lw    $t0, 320+4*\n($a0)
        lw    $t1, 324+4*\n($a0)
        ctc2  $t0, $\n
        fz_ctc2_next \n
        .endr
        .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        lwc2  $\n, 192+4*\n($a0)
        .endr
        # HI, LO
        lw    $t0, 160($a0)
        lw    $t1, 164($a0)
        mthi  $t0
        mtlo  $t1
        # the bases
        lui   $s0, %hi(DATA)
        addiu $s0, $s0, %lo(DATA)
        lui   $at, 0x2000
        or    $s3, $s0, $at
        lui   $s2, 0x1F80
        lw    $k0, 0($a0)              # the case's address
        lw    $k1, 4($a0)              # e
        sw    $k0, 4($sp)
        sw    $k1, 0($sp)
        # the case's own registers (a0 last: it holds the context)
        .irp r,1,2,3,5,6,7,8,9,10,11,12,13,14,15,20,21,22,23,24,25
        lw    $\r, 32+4*\r($a0)
        .endr
        lw    $a0, 32+4*4($a0)
        nop
        # a dummy case first: the wrapper's own lines warm, then the case
        lui   $gp, %hi(fz_dummy)
        addiu $gp, $gp, %lo(fz_dummy)
        bal   fz_timed
        nop
        lw    $gp, 4($sp)
        nop
        bal   fz_timed
        nop
        # k0 = the case's time: save it and every register
        lw    $k1, 12($sp)
        nop
        sw    $k0, 0($k1)
        .irp r,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,28,29,30,31
        sw    $\r, 4*\r($k1)
        .endr
        mfhi  $k0
        sw    $k0, 132($k1)
        mflo  $k0
        sw    $k0, 136($k1)
        .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        swc2  $\n, 140+4*\n($k1)
        .endr
        .irp n,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30
        cfc2  $k0, $\n
        fz_cfc2_next \n
        sw    $k0, 268+4*\n($k1)
        sw    $t0, 272+4*\n($k1)
        .endr
        # every DMA the case started finished (or stopped after a bound): word 99 = 1 if one had to be stopped
        lui   $s1, 0x1F80
        ori   $s1, $s1, 0x1000
        li    $t2, 30000
        lui   $t4, 0x0100
5:      lw    $t0, 0x088($s1)
        lw    $t1, 0x098($s1)
        lw    $t3, 0x0A8($s1)
        or    $t0, $t0, $t1
        or    $t0, $t0, $t3
        lw    $t1, 0x0C8($s1)
        lw    $t3, 0x0E8($s1)
        or    $t0, $t0, $t1
        or    $t0, $t0, $t3
        and   $t0, $t0, $t4
        beqz  $t0, 6f
        addiu $t2, $t2, -1
        bnez  $t2, 5b
        nop
6:      sltiu $t2, $t2, 1
        sw    $t2, 396($k1)
        sw    $zero, 0x088($s1)
        sw    $zero, 0x098($s1)
        sw    $zero, 0x0A8($s1)
        sw    $zero, 0x0C8($s1)
        sw    $zero, 0x0E8($s1)
        # DATA and the scratchpad as the case left them
        lui   $t0, %hi(DATA)
        addiu $t0, $t0, %lo(DATA)
        addiu $t1, $k1, 512
        li    $t2, 512
7:      lw    $t3, 0($t0)
        addiu $t2, $t2, -1
        sw    $t3, 0($t1)
        addiu $t0, $t0, 4
        bnez  $t2, 7b
        addiu $t1, $t1, 4
        lui   $t0, 0x1F80
        addiu $t1, $k1, 2560
        li    $t2, 256
8:      lw    $t3, 0($t0)
        addiu $t2, $t2, -1
        sw    $t3, 0($t1)
        addiu $t0, $t0, 4
        bnez  $t2, 8b
        addiu $t1, $t1, 4
        lw    $ra, 16($sp)
        nop
        jr    $ra
        nop

# the timed call: case at gp. Returns its time in k0.
# The burn: x = 255 - d cycles, as x & 3 nops of a short slide, then x >> 2 turns of a 4-instruction loop - all cached,
# no memory access, so each machine spends a constant plus x, whatever the phase (the dummy pass warmed every line).
fz_timed:
        sw    $ra, 8($sp)
        lw    $k1, 0($sp)              # e
        nop
        lhu   $k0, 0x120($s1)
        nop
        subu  $k0, $k0, $k1            # t - e, Timer 2 wrapping at 49152 (main.cpp): + 49152 if negative, branch-free
        sra   $k1, $k0, 31
        andi  $k1, $k1, 0xC000
        addu  $k0, $k0, $k1
        srl   $k1, $k0, 9              # q = x >> 9 (0..95); q / 3 = (43 q) >> 7, exact below 128
        sll   $ra, $k1, 5
        addu  $ra, $ra, $k1
        sll   $k1, $k1, 1
        addu  $ra, $ra, $k1
        sll   $k1, $k1, 2
        addu  $ra, $ra, $k1
        srl   $ra, $ra, 7
        sll   $k1, $ra, 9              # x - 1536 (q / 3) = x mod 1536
        subu  $k0, $k0, $k1
        sll   $k1, $k1, 1
        subu  $k0, $k0, $k1
        subu  $k0, $zero, $k0
        addiu $k0, $k0, 1535           # x = 1535 - (t - e) mod 1536: the burn
        # the dummy pass burns all of it: a miss fills a line from the missed word on, so a dummy pass entering the slide
        # part-way left the words before its entry invalid, and a later pass entering earlier paid a fill that depended on
        # the phase (the emulator's control copies, 2026-11-13: cold time +-6 until this)
        lui   $k1, %hi(fz_dummy)
        addiu $k1, $k1, %lo(fz_dummy)
        bne   $gp, $k1, 3f
        nop
        addiu $k0, $zero, 1535
3:
        andi  $k1, $k0, 3
        sll   $k1, $k1, 2
        srl   $k0, $k0, 2
        lui   $ra, %hi(fz_slide_end)
        addiu $ra, $ra, %lo(fz_slide_end)
        subu  $ra, $ra, $k1
        jr    $ra
        nop
        nop
        nop
        nop
fz_slide_end:
        beqz  $k0, 2f
        nop
1:      addiu $k0, $k0, -1
        nop
        bnez  $k0, 1b
        nop
2:      lhu   $fp, 0x120($s1)
        jalr  $gp
        nop
        lhu   $k0, 0x120($s1)
        nop
        subu  $k0, $k0, $fp
        andi  $k0, $k0, 0xFFFF
        lw    $ra, 8($sp)
        nop
        jr    $ra
        nop

fz_dummy:
        jr    $ra
        nop

        .balign 16
        .global fz_wrap_end
fz_wrap_end:
