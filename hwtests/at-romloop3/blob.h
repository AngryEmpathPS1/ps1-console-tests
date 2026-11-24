/*
 * hwtests/at-romloop3/blob.h (2026-11-24, the cold-io session) - at-romloop's third blob for the cart's flash: a load whose value a
 * BRANCH reads. WHY: land_romcart2 (at-romloop2's grid: an ALU use within two slots costs 5) leaves the BIOS census's strlen fast
 * here (2048: +1,440 console minus here), and strlen's loop reads its byte with a branch two on (bnez); a branch reads its register
 * a stage earlier than an ALU op, and +1 for it overshoots (-608) - the console's extra is about 0.7 a byte. Same layout as
 * at-romloop's blob.h ('AERLOOP3', 8 functions, 12 words each from +48), same loop: 64 turns of
 *   [x0 x1 x2 x3]  addiu a0, 1  addiu a1, 1  addiu t9, -1  bnez t9  nop      then jr ra / nop
 * with x0..x3 (every branch here is never taken): N nop x4; B1 lbu t0,0(a1) then bne t0,t0 (two registers) one on; B2 two on; B3
 * three on (its delay slot the loop's addiu); Z1 / Z2 bltz t0 (one register, as bnez) one / two on; SLB strlen's shape - lbu t0,
 * addiu t1,t1,1, bltz t0; BB memcmp's - lbu t0,0(a1), lbu t1,0(a0), bne t0,t1 (both buffers zero: equal).
 */
#pragma once
#include <stdint.h>
#define RL_NFN 8
#define RL_WORDS (12 + RL_NFN * 12)
static const char *const RL_NAMES[RL_NFN] = { "N", "B1", "B2", "B3", "Z1", "Z2", "SLB", "BB" };
#define RL_FN(x0, x1, x2, x3) 0x24190040u, x0, x1, x2, x3, 0x24840001u, 0x24A50001u, 0x2739FFFFu, 0x1720FFF8u, 0x00000000u, 0x03E00008u, 0x00000000u
#define RL_LB   0x90A80000u   /* lbu t0, 0(a1) */
#define RL_LBA  0x90890000u   /* lbu t1, 0(a0) */
#define RL_BNE  0x15080000u   /* bne t0, t0, +0 (never taken) */
#define RL_BNE2 0x15090000u   /* bne t0, t1, +0 (equal: never taken) */
#define RL_BLTZ 0x05000000u   /* bltz t0, +0 (a byte: never taken) */
#define RL_INC  0x25290001u   /* addiu t1, t1, 1 */
static const uint32_t RL_BLOB[RL_WORDS] __attribute__((aligned(16))) = {
    0x4C524541u, 0x33504F4Fu, RL_NFN, 48, 96, 144, 192, 240, 288, 336, 384, 0,   /* 'AERLOOP3', 8, the offsets, padding to +48 */
    RL_FN(0, 0, 0, 0),
    RL_FN(RL_LB, RL_BNE, 0, 0),
    RL_FN(RL_LB, 0, RL_BNE, 0),
    RL_FN(RL_LB, 0, 0, RL_BNE),
    RL_FN(RL_LB, RL_BLTZ, 0, 0),
    RL_FN(RL_LB, 0, RL_BLTZ, 0),
    RL_FN(RL_LB, RL_INC, RL_BLTZ, 0),
    RL_FN(RL_LB, RL_LBA, RL_BNE2, 0),
};
