/*
 * gpu/fuzz-gpu - random drawing, read back from VRAM pixel by pixel (2026-11-01)
 *
 * WHY. Nothing here has ever compared a picture a real GPU drew with ours, pixel by pixel, on shapes nobody chose: edges, Gouraud rounding, dither, semi-transparency, texture
 * windows, the mask bit, polylines, rectangles, copies. Every id is a whole program: xorshift32 from 0x9E3779B9 * (id + 1), so a line that differs can be rebuilt anywhere.
 *
 * WHAT IT DOES. Interrupts off, no disc. Random textures at (512,0) and CLUTs at (512,256), once. For each of 2,048 ids: a fill, a random drawing environment (E1 .. E6), 1 to 6
 * random commands in and around a 128 x 128 window at (0,0), wait for the GPU, read the window back through GPUREAD. A line: V id time, then eight 16-bit hashes of the window's
 * bands of 16 rows. time is Timer 2 at system clock / 8, first word to the GPU idle again.
 *
 * READ IT LIKE THIS. A band that differs says where. dump.h names ids whose every pixel is printed instead: R row, then 128 pixels in hex.
 * There are no assertions - compare against your emulator. A wait that times out prints a line with TIMEOUT and goes on.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-gpu --frames=20000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include "dump.h"

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0      0x1F801810u
#define GP1      0x1F801814u
#define IDS 2048
#define WIN 128

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int timeouts;
static bool waitFor(uint32_t bit) {
    for (int i = 0; i < 4000000; i++) if (read32(GP1) & bit) return true;
    timeouts++; printf("TIMEOUT %x stat %x\n", bit, read32(GP1));
    return false;
}
static uint32_t cmd[64];
static int nc;
static void send() { waitFor(1u << 26); for (int i = 0; i < nc; i++) write32(GP0, cmd[i]); nc = 0; }

static int cx, cy, cs;
static uint32_t coord() {
    int x, y;
    if (rnd() % 8 == 0) { x = (int)(rnd() % 512) - 200; y = (int)(rnd() % 400) - 150; }
    else { x = cx + (int)(rnd() % (uint32_t)(cs * 2 + 1)) - cs; y = cy + (int)(rnd() % (uint32_t)(cs * 2 + 1)) - cs; }
    return ((uint32_t)(y & 0x7FF) << 16) | ((uint32_t)x & 0x7FF);
}
static uint32_t col() { return rnd() % 3 ? rnd() & 0xFFFFFF : 0x808080; }
static uint32_t uv() { return rnd() & 0xFFFF; }
static uint32_t clut() { return ((256u + rnd() % 8u) << 6) | (512u >> 4); }
static uint32_t page() { return 8u | ((rnd() % 4u) << 5) | ((rnd() % 3u) << 7); }

static void poly() {
    const uint32_t q = rnd() & 1, g = rnd() & 1, t = rnd() & 1, s = rnd() & 1, r = rnd() & 1;
    const uint32_t op = 0x20u | (g << 4) | (q << 3) | (t << 2) | (s << 1) | (t ? r : 0);
    const int v = q ? 4 : 3;
    for (int i = 0; i < v; i++) {
        if (i == 0) cmd[nc++] = (op << 24) | col();
        else if (g) cmd[nc++] = col();
        cmd[nc++] = coord();
        if (t) cmd[nc++] = uv() | (i == 0 ? clut() << 16 : i == 1 ? page() << 16 : 0);
    }
}
static void line() {
    const uint32_t p = rnd() & 1, g = rnd() & 1, s = rnd() & 1;
    const uint32_t op = 0x40u | (g << 4) | (p << 3) | (s << 1);
    const int v = p ? 2 + (int)(rnd() % 4) : 2;
    for (int i = 0; i < v; i++) {
        if (i == 0) cmd[nc++] = (op << 24) | col();
        else if (g) cmd[nc++] = col();
        cmd[nc++] = coord();
    }
    if (p) cmd[nc++] = 0x55555555u;
}
static void rect() {
    const uint32_t z = rnd() & 3, t = rnd() & 1, s = rnd() & 1, r = rnd() & 1;
    const uint32_t op = 0x60u | (z << 3) | (t << 2) | (s << 1) | (t ? r : 0);
    cmd[nc++] = (op << 24) | col();
    cmd[nc++] = coord();
    if (t) cmd[nc++] = uv() | (clut() << 16);
    if (!z) cmd[nc++] = ((rnd() % 64u) << 16) | (rnd() % 64u);
}
static void other() {
    switch (rnd() % 3) {
        case 0:
            cmd[nc++] = 0x02000000u | col();
            cmd[nc++] = ((rnd() % WIN) << 16) | (rnd() % WIN);
            cmd[nc++] = ((rnd() % 64u) << 16) | (rnd() % 64u);
            break;
        case 1:
            cmd[nc++] = 0x80000000u;
            cmd[nc++] = ((rnd() % WIN) << 16) | (rnd() % WIN);
            cmd[nc++] = ((rnd() % WIN) << 16) | (rnd() % WIN);
            cmd[nc++] = ((1 + rnd() % 32u) << 16) | (1 + rnd() % 32u);
            break;
        default:
            cmd[nc++] = 0xA0000000u;
            cmd[nc++] = ((rnd() % WIN) << 16) | (rnd() % WIN);
            cmd[nc++] = (4u << 16) | 4u;
            for (int i = 0; i < 8; i++) cmd[nc++] = rnd();
            break;
    }
}
static void uploadTextures() {
    rng = 0x12345678u;
    nc = 0; cmd[nc++] = 0xA0000000u; cmd[nc++] = 512u; cmd[nc++] = (256u << 16) | 256u; send();
    for (int i = 0; i < 256 * 128; i++) { waitFor(1u << 28); uint32_t w = rnd(); if (!(i % 11)) w &= 0xFFFF0000u; write32(GP0, w); }
    nc = 0; cmd[nc++] = 0xA0000000u; cmd[nc++] = (256u << 16) | 512u; cmd[nc++] = (8u << 16) | 256u; send();
    for (int i = 0; i < 256 * 4; i++) { waitFor(1u << 28); uint32_t w = rnd(); if (!(i % 7)) w &= 0x0000FFFFu; write32(GP0, w); }
}
static uint32_t buf[WIN * WIN / 2];
static void readWindow() {
    nc = 0; cmd[nc++] = 0x01000000u; cmd[nc++] = 0xC0000000u; cmd[nc++] = 0; cmd[nc++] = ((uint32_t)WIN << 16) | WIN; send();
    if (!waitFor(1u << 27)) return;
    for (int i = 0; i < WIN * WIN / 2; i++) buf[i] = read32(GP0);
}
static void program(int id) {
    rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
    nc = 0; cmd[nc++] = 0x02000000u | col(); cmd[nc++] = 0; cmd[nc++] = (256u << 16) | 256u; send();
    uint32_t x1 = 0, y1 = 0, x2 = WIN - 1, y2 = WIN - 1;
    if (rnd() % 4 == 0) { x1 = rnd() % 64u; y1 = rnd() % 64u; x2 = x1 + rnd() % 128u; y2 = y1 + rnd() % 128u; }
    const int ox = (rnd() & 1) ? (int)(rnd() % 65) - 32 : 0, oy = (rnd() & 1) ? (int)(rnd() % 65) - 32 : 0;
    nc = 0;
    cmd[nc++] = 0xE1000000u | page() | ((rnd() & 1) << 9) | ((rnd() & 1) << 10) | ((rnd() % 8 == 0 ? rnd() & 3 : 0) << 12);
    cmd[nc++] = 0xE2000000u | (rnd() % 5 == 0 ? rnd() & 0xFFFFF : 0);
    cmd[nc++] = 0xE3000000u | (y1 << 10) | x1;
    cmd[nc++] = 0xE4000000u | (y2 << 10) | x2;
    cmd[nc++] = 0xE5000000u | (((uint32_t)oy & 0x7FF) << 11) | ((uint32_t)ox & 0x7FF);
    cmd[nc++] = 0xE6000000u | (rnd() % 4 == 0 ? rnd() & 3 : 0);
    send();
    static const int SC[4] = { 8, 24, 64, 160 };
    const int n = 1 + (int)(rnd() % 6);
    write16(T2_COUNT, 0);
    for (int i = 0; i < n; i++) {
        cx = (int)(rnd() % 160) - 16; cy = (int)(rnd() % 160) - 16; cs = SC[rnd() % 4];
        const uint32_t k = rnd() % 16;
        if (k < 8) poly(); else if (k < 11) line(); else if (k < 14) rect(); else other();
        send();
    }
    waitFor(1u << 26);
    const uint16_t t = read16(T2_COUNT);
    readWindow();
    printf("V %d %u", id, t);
    uint32_t p = 0;
    const uint16_t* px = (const uint16_t*)buf;
    for (int b = 0; b < 8; b++) {
        uint32_t h = 2166136261u;
        for (int i = 0; i < 16 * WIN; i++) h = (h ^ px[p++]) * 16777619u;
        printf(" %04x", (h ^ (h >> 16)) & 0xFFFF);
    }
    printf("\n");
}

int main() {
    printf("\ngpu/fuzz-gpu\n");
    printf("%d programs, window %d x %d: V id time and eight band hashes. No assertions.\n\n", IDS, WIN, WIN);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200);        /* system clock / 8 */
    write32(GP1, 0x01000000u);       /* the command buffer, emptied */
    uploadTextures();
    if (DUMP[0] >= 0) {
        for (int k = 0; DUMP[k] >= 0; k++) {
            program(DUMP[k]);
            const uint16_t* px = (const uint16_t*)buf;
            for (int y = 0; y < WIN; y++) { printf("R %d", y); for (int x = 0; x < WIN; x++) printf(" %x", px[y * WIN + x]); printf("\n"); }
        }
    } else for (int id = 0; id < IDS; id++) program(id);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ngpu/fuzz-gpu: Done.\n", timeouts);
    for (;;) VSync(0);
}
