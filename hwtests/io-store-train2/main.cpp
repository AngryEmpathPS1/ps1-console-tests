/*
 * cpu/io-store-train2 - when does each of a train of I/O stores land? (experiments/mk_iotrain.js; 2026-11-14)
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
static void __attribute__((noinline, aligned(64))) k0g0p2(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g0p2(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g0p2(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k0g0p3(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g0p3(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g0p3(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k0g0p4(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g0p4(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g0p4(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k0g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k1g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) k2g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
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
static void __attribute__((noinline, aligned(64))) r4g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sw $zero, 8(%5)\n sw $zero, 12(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r1g0p4(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r2g0p4(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r4g0p4(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sw $zero, 8(%5)\n sw $zero, 12(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r1g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r2g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) r4g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sw $zero, 4(%5)\n sw $zero, 8(%5)\n sw $zero, 12(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) ri1g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) ir1g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw %6, 0(%4)\n sw $zero, 0(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) rri1g0p1(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n sw $zero, 0(%5)\n sw $zero, 0(%5)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) ri1g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) ir1g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw %6, 0(%4)\n sw $zero, 0(%5)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }
static void __attribute__((noinline, aligned(64))) rri1g0p8(uint32_t *o) {
    uint32_t a, b, c;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%3)\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n sw $zero, 0(%5)\n sw $zero, 0(%5)\n sw %6, 0(%4)\n sh $zero, 0(%7)\n .rept 40\n nop\n .endr\n lhu %1, 0(%7)\n lhu %2, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b), "=&r"(c) : "r"(T1_COUNT), "r"(D4_MADR), "r"(ramWords), "r"(MADRV), "r"(T2_COUNT) : "memory");
    o[0] = (c - a) & 0xFFFF; o[1] = b & 0xFFFF; }

static const struct { const char *n; Row f; } ROWS[] = {
    { "K0 P1", k0g0p1 },
    { "K1 P1", k1g0p1 },
    { "K2 P1", k2g0p1 },
    { "K0 P2", k0g0p2 },
    { "K1 P2", k1g0p2 },
    { "K2 P2", k2g0p2 },
    { "K0 P3", k0g0p3 },
    { "K1 P3", k1g0p3 },
    { "K2 P3", k2g0p3 },
    { "K0 P4", k0g0p4 },
    { "K1 P4", k1g0p4 },
    { "K2 P4", k2g0p4 },
    { "K0 P8", k0g0p8 },
    { "K1 P8", k1g0p8 },
    { "K2 P8", k2g0p8 },
    { "R1 P1", r1g0p1 },
    { "R2 P1", r2g0p1 },
    { "R4 P1", r4g0p1 },
    { "R1 P4", r1g0p4 },
    { "R2 P4", r2g0p4 },
    { "R4 P4", r4g0p4 },
    { "R1 P8", r1g0p8 },
    { "R2 P8", r2g0p8 },
    { "R4 P8", r4g0p8 },
    { "RI P1", ri1g0p1 },
    { "IR P1", ir1g0p1 },
    { "RRI P1", rri1g0p1 },
    { "RI P8", ri1g0p8 },
    { "IR P8", ir1g0p8 },
    { "RRI P8", rri1g0p8 },
};
#define NROWS (sizeof(ROWS) / sizeof(ROWS[0]))

static Row uncached(Row f) { return (Row)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

int main() {
    printf("\ncpu/io-store-train2\n");
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
    printf("\ncpu/io-store-train2: Done.\n");
    for (;;) VSync(0);
}
