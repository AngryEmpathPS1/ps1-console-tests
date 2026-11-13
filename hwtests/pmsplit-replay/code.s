# pmsplit-replay - code. rp_launch(regs, entry, count) saves the driver's registers, starts Timer 2 at system clock / 8, loads
# r1..r31, hi, lo from regs[] (pmsplit-sp's registers at S), k1 = count, and jumps to entry; the stub at the end PC counts k1
# down and leaves through rp_exit, which reads Timer 2 first and returns its count to the driver. 2026-11-13, Claude (AI) for
# AngryEmpathPS1.
        .set noreorder
        .set noat
        .text

        .globl on_stack                   # on_stack(fn, top): fn() on its own stack (it never returns)
on_stack:
        move    $sp, $a1
        jr      $a0
        nop

        .globl bios_flush                 # A(44h) FlushCache
bios_flush:
        addiu   $t2, $zero, 0xa0
        jr      $t2
        addiu   $t1, $zero, 0x44

        .balign 16
        .globl rp_launch
rp_launch:
        la      $t0, rp_save
        sw      $s0, 0($t0)
        sw      $s1, 4($t0)
        sw      $s2, 8($t0)
        sw      $s3, 12($t0)
        sw      $s4, 16($t0)
        sw      $s5, 20($t0)
        sw      $s6, 24($t0)
        sw      $s7, 28($t0)
        sw      $s8, 32($t0)
        sw      $gp, 36($t0)
        sw      $sp, 40($t0)
        sw      $ra, 44($t0)
        lw      $t0, 128($a0)
        nop
        mthi    $t0
        lw      $t0, 132($a0)
        nop
        mtlo    $t0
        move    $k0, $a1
        move    $k1, $a2
        lui     $t0, 0x1f80
        addiu   $t1, $zero, 0x0200        # Timer 2: system clock / 8, counting from 0 (a mode write resets the count)
        sw      $t1, 0x1124($t0)
        addiu   $t1, $zero, 0x0100        # Timer 1: horizontal blanks, from 0 (for runs past Timer 2's 524,280 cycles)
        sw      $t1, 0x1114($t0)
        lw      $1, 4($a0)
        lw      $2, 8($a0)
        lw      $3, 12($a0)
        lw      $5, 20($a0)
        lw      $6, 24($a0)
        lw      $7, 28($a0)
        lw      $8, 32($a0)
        lw      $9, 36($a0)
        lw      $10, 40($a0)
        lw      $11, 44($a0)
        lw      $12, 48($a0)
        lw      $13, 52($a0)
        lw      $14, 56($a0)
        lw      $15, 60($a0)
        lw      $16, 64($a0)
        lw      $17, 68($a0)
        lw      $18, 72($a0)
        lw      $19, 76($a0)
        lw      $20, 80($a0)
        lw      $21, 84($a0)
        lw      $22, 88($a0)
        lw      $23, 92($a0)
        lw      $24, 96($a0)
        lw      $25, 100($a0)
        lw      $28, 112($a0)
        lw      $29, 116($a0)
        lw      $30, 120($a0)
        lw      $31, 124($a0)
        lw      $4, 16($a0)
        jr      $k0
        nop

        .balign 16
        .globl rp_exit
rp_exit:
        lui     $k0, 0x1f80
        lw      $k1, 0x1110($k0)
        lw      $k0, 0x1120($k0)
        la      $at, rp_t1
        sw      $k1, 0($at)
        la      $k1, rp_save
        lw      $s0, 0($k1)
        lw      $s1, 4($k1)
        lw      $s2, 8($k1)
        lw      $s3, 12($k1)
        lw      $s4, 16($k1)
        lw      $s5, 20($k1)
        lw      $s6, 24($k1)
        lw      $s7, 28($k1)
        lw      $s8, 32($k1)
        lw      $gp, 36($k1)
        lw      $sp, 40($k1)
        lw      $ra, 44($k1)
        andi    $v0, $k0, 0xffff
        jr      $ra
        nop

        .data
        .balign 4
rp_save: .space 48
        .globl rp_t1
rp_t1:  .space 4
