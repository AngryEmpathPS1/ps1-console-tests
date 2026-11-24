/*
 * hwtests/at-romloop2/blob.h (2026-11-23, the cold-io session) - at-romloop's second blob for the cart's flash: loads whose value is
 * USED, two loads, two stores and the copy loop's shapes. WHY: land_romcart (at-romloop's grid) left the BIOS census rows whose loads
 * are used at once untouched - strlen 2048 -720, memcmp / bcmp 256 -547, rindex -376, strcmp -333 (console minus here: slower here);
 * this core holds only a load whose next two neither read nor write its register (uncHoldLoad), and charges any other one a serial
 * RAM read. Same layout as at-romloop's blob.h ('AERLOOP2', 8 functions, 12 words each from +48), same loop: 64 turns of
 *   [x0 x1 x2 x3]  addiu a0, 1  addiu a1, 1  addiu t9, -1  bnez t9  nop      then jr ra / nop
 * with x0..x3: N nop x4; U1 lbu t0,0(a1) then addu t1,t0,zero in the load delay slot; U2 the use two on; U3 three on; LL lbu t0 then
 * lbu t1,0(a1); SB2 sb zero,0(a0) then sb zero,1(a0); CP1 lbu t0 then sb t0,0(a0) (the delay slot: the old t0); CP2 lbu, nop, sb t0.
 */
#pragma once
#include <stdint.h>
#define RL_NFN 8
#define RL_WORDS (12 + RL_NFN * 12)
static const char *const RL_NAMES[RL_NFN] = { "N", "U1", "U2", "U3", "LL", "SB2", "CP1", "CP2" };
#define RL_FN(x0, x1, x2, x3) 0x24190040u, x0, x1, x2, x3, 0x24840001u, 0x24A50001u, 0x2739FFFFu, 0x1720FFF8u, 0x00000000u, 0x03E00008u, 0x00000000u
#define RL_LB  0x90A80000u   /* lbu t0, 0(a1) */
#define RL_LB1 0x90A90000u   /* lbu t1, 0(a1) */
#define RL_USE 0x01004821u   /* addu t1, t0, zero */
#define RL_SB  0xA0800000u   /* sb zero, 0(a0) */
#define RL_SB1 0xA0800001u   /* sb zero, 1(a0) */
#define RL_ST0 0xA0880000u   /* sb t0, 0(a0) */
static const uint32_t RL_BLOB[RL_WORDS] __attribute__((aligned(16))) = {
    0x4C524541u, 0x32504F4Fu, RL_NFN, 48, 96, 144, 192, 240, 288, 336, 384, 0,   /* 'AERLOOP2', 8, the offsets, padding to +48 */
    RL_FN(0, 0, 0, 0),
    RL_FN(RL_LB, RL_USE, 0, 0),
    RL_FN(RL_LB, 0, RL_USE, 0),
    RL_FN(RL_LB, 0, 0, RL_USE),
    RL_FN(RL_LB, RL_LB1, 0, 0),
    RL_FN(RL_SB, RL_SB1, 0, 0),
    RL_FN(RL_LB, RL_ST0, 0, 0),
    RL_FN(RL_LB, 0, RL_ST0, 0),
};
