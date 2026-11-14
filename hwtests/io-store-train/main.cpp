/*
 * cpu/io-store-train - when does each of a train of I/O stores land? (experiments/mk_iotrain.js; 2026-11-14)
 *
 * WHY. hwtests/dma-otc-latch (the GPU session) says back-to-back I/O stores land a cycle later each on the console than in this
 * emulator, and the fuzzer's otc2902 says in uncached code this emulator lands two I/O stores two instructions apart at the same
 * cycle where the console has them more than 4 apart. Neither test times a store directly.
 * WHAT IT DOES. Timer 1 and Timer 2 free-running at the system clock, interrupts off. Each row: read Timer 1, K word stores to
 * DMA4's MADR (idle) with G nops after each, a halfword store of 0 to Timer 2's counter, 40 nops, read Timer 2, read Timer 1.
 * R rows store to RAM instead. Each row from cached code (its second call) and uncached code (its KSEG1 alias), 3 times.
 * READ IT LIKE THIS. "land" = Timer 1's elapsed - Timer 2's value: when the Timer 2 store took effect, from the first read (plus a
 * constant for the two reads at the end). Against K 0, what each I/O store ahead of it costs; against G, how the gaps hide it.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define D4_MADR  0x1F8010C0u
#define MADRV    0x00100000u

static volatile uint32_t ramWords[8];
typedef void (*Row)(uint32_t *);
static void __attribute__((noinline, aligned(64))) k0g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k3g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k4g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k5g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sw %6, 0(%4)\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g1p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g1p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k3g1p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k4g1p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k5g1p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sw %6, 0(%4)\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g2p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g2p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k3g2p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k4g2p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k5g2p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g4p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g4p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k3g4p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k4g4p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k5g4p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n nop\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r1g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r2g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r3g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sw $zero, 8(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r4g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sw $zero, 8(%5)\n sw $zero, 12(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r5g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sw $zero, 8(%5)\n sw $zero, 12(%5)\n sw $zero, 16(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }

static const struct { const char *n; Row f; } ROWS[] = {
    { "K0 G0", k0g0p1 },
    { "K1 G0", k1g0p1 },
    { "K2 G0", k2g0p1 },
    { "K3 G0", k3g0p1 },
    { "K4 G0", k4g0p1 },
    { "K5 G0", k5g0p1 },
    { "K1 G1", k1g1p1 },
    { "K2 G1", k2g1p1 },
    { "K3 G1", k3g1p1 },
    { "K4 G1", k4g1p1 },
    { "K5 G1", k5g1p1 },
    { "K1 G2", k1g2p1 },
    { "K2 G2", k2g2p1 },
    { "K3 G2", k3g2p1 },
    { "K4 G2", k4g2p1 },
    { "K5 G2", k5g2p1 },
    { "K1 G4", k1g4p1 },
    { "K2 G4", k2g4p1 },
    { "K3 G4", k3g4p1 },
    { "K4 G4", k4g4p1 },
    { "K5 G4", k5g4p1 },
    { "R1 G0", r1g0p1 },
    { "R2 G0", r2g0p1 },
    { "R3 G0", r3g0p1 },
    { "R4 G0", r4g0p1 },
    { "R5 G0", r5g0p1 },
};
#define NROWS (sizeof(ROWS) / sizeof(ROWS[0]))

static Row uncached(Row f) { return (Row)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

int main() {
    printf("\ncpu/io-store-train\n");
    printf("K stores to DMA4 MADR (G nops after each), then Timer 2 := 0; land = Timer 1 elapsed - Timer 2.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint32_t m1 = read16(T1_MODE), m2 = read16(T2_MODE), madr = read32(D4_MADR);
    write16(T1_MODE, 0); write16(T2_MODE, 0);
    static uint32_t out[2][3][NROWS][2];
    for (int u = 0; u < 2; u++) for (int rep = 0; rep < 3; rep++) for (unsigned i = 0; i < NROWS; i++) {
        Row g = u ? uncached(ROWS[i].f) : ROWS[i].f;
        g(out[u][rep][i]);
        g(out[u][rep][i]);
    }
    write32(D4_MADR, madr); write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int u = 0; u < 2; u++) {
        printf("%s\n", u ? "uncached" : "cached");
        for (unsigned i = 0; i < NROWS; i++) {
            printf("  %-7s land", ROWS[i].n);
            for (int rep = 0; rep < 3; rep++) printf(" %4d", (int)(out[u][rep][i][0] - out[u][rep][i][1]));
            printf("   elapsed");
            for (int rep = 0; rep < 3; rep++) printf(" %4d", (int)out[u][rep][i][0]);
            printf("\n");
        }
    }
    printf("\ncpu/io-store-train: Done.\n");
    for (;;) VSync(0);
}
