/*
 * hwtests/at-romloop/blob.h (2026-11-23, the cold-io session) - the loops to put in the cart's flash, as words. The same words are
 * this test's RAM copy (the uncached and cached controls) and what it checks the cart for. Position-independent (relative branches
 * only), 4-byte aligned; functions on 16-byte boundaries. Layout (byte offsets from the blob's start):
 *   +0   'AERL' 'OOP1' (41 45 52 4C 4F 4F 50 31)       +8  the function count (7)       +12 .. +39  each function's byte offset
 *   +48  the functions, 12 words (48 bytes) each, in the order of NAMES: N, S0, S1, S2, S3, L0, SL.
 * Each function: a0 = a RAM buffer it stores to, a1 = a RAM buffer it loads from; 64 turns of
 *   [x0 x1 x2 x3]  addiu a0, 1  addiu a1, 1  addiu t9, -1  bnez t9  nop      then jr ra / nop
 * with x0..x3: N nop nop nop nop; S0 sb zero,0(a0) nop nop nop (a store alone); S1 lbu t0,0(a1) sb zero,0(a0) nop nop (the store 1
 * behind the load); S2 lbu nop sb nop (2 behind); S3 lbu nop nop sb (3 behind); L0 lbu nop nop nop (a load alone); SL sb lbu nop nop.
 */
#pragma once
#include <stdint.h>
#define RL_NFN 7
#define RL_WORDS (12 + RL_NFN * 12)
static const char *const RL_NAMES[RL_NFN] = { "N", "S0", "S1", "S2", "S3", "L0", "SL" };
#define RL_FN(x0, x1, x2, x3) 0x24190040u, x0, x1, x2, x3, 0x24840001u, 0x24A50001u, 0x2739FFFFu, 0x1720FFF8u, 0x00000000u, 0x03E00008u, 0x00000000u
#define RL_SB 0xA0800000u   /* sb zero, 0(a0) */
#define RL_LB 0x90A80000u   /* lbu t0, 0(a1) */
static const uint32_t RL_BLOB[RL_WORDS] __attribute__((aligned(16))) = {
    0x4C524541u, 0x31504F4Fu, RL_NFN, 48, 96, 144, 192, 240, 288, 336, 0, 0,   /* 'AERLOOP1', 7, the offsets, padding to +48 */
    RL_FN(0, 0, 0, 0),
    RL_FN(RL_SB, 0, 0, 0),
    RL_FN(RL_LB, RL_SB, 0, 0),
    RL_FN(RL_LB, 0, RL_SB, 0),
    RL_FN(RL_LB, 0, 0, RL_SB),
    RL_FN(RL_LB, 0, 0, 0),
    RL_FN(RL_SB, RL_LB, 0, 0),
};
