# cpu/irq-unmask3 - code (handler and SR helpers as irq-unmask). A pending VBlank (I_STAT.0 latched while I_MASK 0), SR.IEc and IM2 on; each variant stores I_MASK = 1
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

        .globl v_w0k0
v_w0k0:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k0_after
v_w0k0_after:
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

        .globl v_w0k1
v_w0k1:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k1_after
v_w0k1_after:
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

        .globl v_w0k2
v_w0k2:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k2_after
v_w0k2_after:
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

        .globl v_w0k3
v_w0k3:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k3_after
v_w0k3_after:
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

        .globl v_w0k4
v_w0k4:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k4_after
v_w0k4_after:
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

        .globl v_w0k5
v_w0k5:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k5_after
v_w0k5_after:
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

        .globl v_w0k6
v_w0k6:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k6_after
v_w0k6_after:
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

        .globl v_w0k7
v_w0k7:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k7_after
v_w0k7_after:
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

        .globl v_w0k8
v_w0k8:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k8_after
v_w0k8_after:
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

        .globl v_w0k9
v_w0k9:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k9_after
v_w0k9_after:
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

        .globl v_w0k10
v_w0k10:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k10_after
v_w0k10_after:
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

        .globl v_w0k11
v_w0k11:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k11_after
v_w0k11_after:
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

        .globl v_w0k12
v_w0k12:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k12_after
v_w0k12_after:
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

        .globl v_w0k13
v_w0k13:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k13_after
v_w0k13_after:
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

        .globl v_w0k14
v_w0k14:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k14_after
v_w0k14_after:
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

        .globl v_w0k15
v_w0k15:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w0k15_after
v_w0k15_after:
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

        .globl v_w2k0
v_w2k0:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        sw      $t1, 0x1074($t0)
        .globl v_w2k0_after
v_w2k0_after:
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

        .globl v_w2k1
v_w2k1:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k1_after
v_w2k1_after:
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

        .globl v_w2k2
v_w2k2:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k2_after
v_w2k2_after:
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

        .globl v_w2k3
v_w2k3:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k3_after
v_w2k3_after:
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

        .globl v_w2k4
v_w2k4:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k4_after
v_w2k4_after:
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

        .globl v_w2k5
v_w2k5:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k5_after
v_w2k5_after:
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

        .globl v_w2k6
v_w2k6:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k6_after
v_w2k6_after:
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

        .globl v_w2k7
v_w2k7:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k7_after
v_w2k7_after:
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

        .globl v_w2k8
v_w2k8:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k8_after
v_w2k8_after:
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

        .globl v_w2k9
v_w2k9:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k9_after
v_w2k9_after:
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

        .globl v_w2k10
v_w2k10:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k10_after
v_w2k10_after:
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

        .globl v_w2k11
v_w2k11:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k11_after
v_w2k11_after:
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

        .globl v_w2k12
v_w2k12:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k12_after
v_w2k12_after:
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

        .globl v_w2k13
v_w2k13:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k13_after
v_w2k13_after:
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

        .globl v_w2k14
v_w2k14:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k14_after
v_w2k14_after:
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

        .globl v_w2k15
v_w2k15:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scr)
        addiu   $t2, $t2, %lo(irq_scr)
        lw      $t3, 0x1070($t0)          # an I/O read: the write buffer empty, the CPU on the bus's beat
        nop
        sw      $zero, 0($t2)
        sw      $zero, 0($t2)
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        sw      $t1, 0x1074($t0)
        .globl v_w2k15_after
v_w2k15_after:
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

        .data
        .balign 4
        .globl irq_rec
irq_rec:
        .word 0, 0, 0, 0
irq_scr:
        .word 0, 0, 0, 0
