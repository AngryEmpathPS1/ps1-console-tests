# cpu/irq-unmask - code. A pending VBlank (I_STAT.0 latched while I_MASK 0), SR.IEc and IM2 on; each variant stores I_MASK = 1
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

# MLB 99's SetIntrMask exactly (80021170h): the pointer from a variable, the old value read, the new stored in the slot before
# the return.
        .globl v_setmask
v_setmask:
        lui     $v1, %hi(imask_ptr)
        lw      $v1, %lo(imask_ptr)($v1)
        nop
        lhu     $v0, 0($v1)
        sh      $a0, 0($v1)
        jr      $ra
        nop

        .globl v_game                     # its caller (8001FADCh): jal, nop, and on
v_game:
        addiu   $sp, $sp, -8
        sw      $ra, 4($sp)
        move    $t5, $zero
        addiu   $a0, $zero, 1
        jal     v_setmask
        nop
        .globl v_game_ret
v_game_ret:
        .rept 16
        addiu   $t5, $t5, 1
        .endr
        lw      $ra, 4($sp)
        addiu   $sp, $sp, 8
        jr      $ra
        move    $v0, $t5

        .globl v_sw                       # a plain word store, then the count
v_sw:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        sw      $t1, 0x1074($t0)
        .globl v_sw_after
v_sw_after:
        .rept 16
        addiu   $t5, $t5, 1
        .endr
        jr      $ra
        move    $v0, $t5

        .globl v_sh                       # a halfword store, then the count
v_sh:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        sh      $t1, 0x1074($t0)
        .globl v_sh_after
v_sh_after:
        .rept 16
        addiu   $t5, $t5, 1
        .endr
        jr      $ra
        move    $v0, $t5

        .globl v_wbuf                     # three RAM stores ahead of it in the write buffer
v_wbuf:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scratch)
        addiu   $t2, $t2, %lo(irq_scratch)
        sw      $zero, 0($t2)
        sw      $zero, 4($t2)
        sw      $zero, 8($t2)
        sw      $t1, 0x1074($t0)
        .globl v_wbuf_after
v_wbuf_after:
        .rept 16
        addiu   $t5, $t5, 1
        .endr
        jr      $ra
        move    $v0, $t5

        .globl v_lw                       # a RAM load right behind it
v_lw:
        move    $t5, $zero
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 1
        lui     $t2, %hi(irq_scratch)
        addiu   $t2, $t2, %lo(irq_scratch)
        sw      $t1, 0x1074($t0)
        .globl v_lw_after
v_lw_after:
        lw      $t3, 0($t2)
        .rept 15
        addiu   $t5, $t5, 1
        .endr
        jr      $ra
        move    $v0, $t5

        .data
        .balign 4
        .globl irq_rec
irq_rec:
        .word 0, 0, 0, 0
imask_ptr:
        .word 0x1f801074
irq_scratch:
        .word 0, 0, 0, 0
