# cpu/irq-unmask2 - code (the handler and SR helpers are irq-unmask's). A pending VBlank (I_STAT.0 latched while I_MASK 0), SR.IEc and IM2 on; each variant stores I_MASK = 1
# and then counts in t5; our exception vector (installed by main.cpp at 80000080h) keeps t5, EPC and Cause at the interrupt.
# 2026-11-06, Claude (AI) for AngryEmpathPS1.
        .set noreorder
        .set noat
        .text

        .globl irq_handler
irq_handler:
        lui     $k0, %hi(irq_rec)
        addiu   $k0, $k0, %lo(irq_rec)
        sw      $t5, 0($k0)
        mfc0    $k1, $14
        nop
        sw      $k1, 4($k0)
        mfc0    $k1, $13
        nop
        sw      $k1, 8($k0)
        lw      $k1, 12($k0)
        nop
        addiu   $k1, $k1, 1
        sw      $k1, 12($k0)
        lui     $k1, 0x1f80
        sw      $zero, 0x1074($k1)        # I_MASK 0: it must not come again
        addiu   $k0, $zero, -2
        sw      $k0, 0x1070($k1)          # ack I_STAT.0
        lw      $k0, 0x1074($k1)          # the read waits the two writes out before RFE turns interrupts back on
        mfc0    $k0, $14
        nop
        jr      $k0
        rfe

        .globl bios_flush                 # A(44h) FlushCache
bios_flush:
        addiu   $t2, $zero, 0xa0
        jr      $t2
        addiu   $t1, $zero, 0x44

        .globl irq_ien                    # SR |= IEc | IM2
irq_ien:
        mfc0    $t0, $12
        nop
        ori     $t0, $t0, 0x401
        mtc0    $t0, $12
        nop
        jr      $ra
        nop

        .globl irq_ioff                   # SR &= ~IEc
irq_ioff:
        mfc0    $t0, $12
        nop
        srl     $t0, $t0, 1
        sll     $t0, $t0, 1
        mtc0    $t0, $12
        nop
        jr      $ra
        nop

        .globl v_add
v_add:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        sw      $t1, 0x1074($t0)
        .globl v_add_after
v_add_after:
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        jr      $ra
        move    $v0, $t5

        .globl v_sb
v_sb:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        sb      $t1, 0x1074($t0)
        .globl v_sb_after
v_sb_after:
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        jr      $ra
        move    $v0, $t5

        .globl v_ioread
v_ioread:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        lw      $t3, 0x1074($t0)
        sw      $t1, 0x1074($t0)
        .globl v_ioread_after
v_ioread_after:
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        addiu   $t5, $t5, 1
        jr      $ra
        move    $v0, $t5

        .globl v_lw
v_lw:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        sw      $t1, 0x1074($t0)
        .globl v_lw_after
v_lw_after:
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        lw      $t3, 0($t2)
        addiu   $t5, $t5, 1
        jr      $ra
        move    $v0, $t5

        .globl v_lwuse
v_lwuse:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        sw      $t1, 0x1074($t0)
        .globl v_lwuse_after
v_lwuse_after:
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        lw      $t3, 0($t2)
        nop
        addu    $t5, $t5, $t3
        jr      $ra
        move    $v0, $t5

        .globl v_branch
v_branch:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        sw      $t1, 0x1074($t0)
        .globl v_branch_after
v_branch_after:
        b       1f
        addiu   $t5, $t5, 1
1:
        b       1f
        addiu   $t5, $t5, 1
1:
        b       1f
        addiu   $t5, $t5, 1
1:
        b       1f
        addiu   $t5, $t5, 1
1:
        b       1f
        addiu   $t5, $t5, 1
1:
        b       1f
        addiu   $t5, $t5, 1
1:
        b       1f
        addiu   $t5, $t5, 1
1:
        b       1f
        addiu   $t5, $t5, 1
1:
        jr      $ra
        move    $v0, $t5

        .globl v_mult
v_mult:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        sw      $t1, 0x1074($t0)
        .globl v_mult_after
v_mult_after:
        mult    $t1, $t1
        mflo    $t3
        addiu   $t5, $t5, 1
        mult    $t1, $t1
        mflo    $t3
        addiu   $t5, $t5, 1
        mult    $t1, $t1
        mflo    $t3
        addiu   $t5, $t5, 1
        mult    $t1, $t1
        mflo    $t3
        addiu   $t5, $t5, 1
        mult    $t1, $t1
        mflo    $t3
        addiu   $t5, $t5, 1
        mult    $t1, $t1
        mflo    $t3
        addiu   $t5, $t5, 1
        jr      $ra
        move    $v0, $t5

        .globl v_ioafter
v_ioafter:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_one)
        addiu   $t2, $t2, %lo(irq_one)
        sw      $t1, 0x1074($t0)
        .globl v_ioafter_after
v_ioafter_after:
        lw      $t3, 0x1070($t0)
        addiu   $t5, $t5, 1
        lw      $t3, 0x1070($t0)
        addiu   $t5, $t5, 1
        lw      $t3, 0x1070($t0)
        addiu   $t5, $t5, 1
        lw      $t3, 0x1070($t0)
        addiu   $t5, $t5, 1
        lw      $t3, 0x1070($t0)
        addiu   $t5, $t5, 1
        lw      $t3, 0x1070($t0)
        addiu   $t5, $t5, 1
        jr      $ra
        move    $v0, $t5

        .data
        .balign 4
        .globl irq_rec
irq_rec:
        .word 0, 0, 0, 0
irq_one:
        .word 1, 1, 1, 1
