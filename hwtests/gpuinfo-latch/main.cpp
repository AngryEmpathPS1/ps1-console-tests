/*
 * gpu/gpuinfo-latch - what does GP1(10h) leave in GPUREAD, and does it
 * depend on what put the old value there?
 *
 * WHY (ROAD A8 "GP1(10h) bit 31", dug 2026-09-27; ROAD B). Two console tests
 * disagree about an index that "Returns Nothing" (psx-spx: 00h, 01h, 06h,
 * 09h-0Fh):
 *   amidog psxtest_gpu REG (M97, 2,496 compares): only BIT 31 of the old
 *   latch survives; 02h-04h keep bits 20-30 and clear bit 31, 05h keeps
 *   22-30; 07h and 08h replace the word. This emulator has exactly that.
 *   nocash's DIAG, GPU INFO: an unused index keeps the WHOLE latch - one of
 *   the three DIAG lines this emulator reads BAD.
 * The dig found why both can be right: DIAG zeroes the latch with a VRAM read
 * of 0 and then fills it only with GP1(10h) answers (DIAG.A22 10676-10846),
 * so it never sees bit 31; amidog fills it through GP0(C0h) VRAM reads.
 * mednafen keeps TWO registers (DataReadBuffer and DataReadBufferEx), which
 * would let both tests pass on one console. Sony calls the command
 * "purposefully not documented".
 *
 * WHAT IT DOES. Known texture window, drawing area and offset (E2h-E5h). Then
 * six sequences, GPUREAD read after every step (64 nops after each GP1):
 *   A  a VRAM read of 80008000h, then indices 00h, 06h, 0Fh
 *   B  a VRAM read of 12345678h, then index 00h
 *   C  index 07h (the GPU's type), then indices 00h and 06h
 *   D  a VRAM read of 9234D678h, then index 05h (the offset), then 00h
 *   E  a VRAM read of FFFFFFFFh, then 02h, 03h, 04h, then 00h
 *   F  index 08h, then 00h
 * A VRAM read here is GP0(C0h) of two pixels (one word), read out of GPUREAD.
 *
 * READ IT LIKE THIS. amidog's rule: A 80000000h three times, B 0, C 2 then 0
 * 0, and the used indices keep the old bits 20-30. DIAG's rule: an unused
 * index changes nothing - A 80008000h, B 12345678h, C 2 2 2. Two registers
 * (mednafen): a VRAM read and a GP1 answer do not overwrite each other, so an
 * unused index after a VRAM read and one after an answer read differently.
 *
 * ON THIS CORE AT v0.258 (`node hwtests/run.js gpuinfo-latch`), amidog's rule:
 *   A  latch 80008000, then 80000000 80000000 80000000
 *   B  latch 12345678, then 00000000
 *   C  00000002, then 00000000 00000000       (DIAG's console: 2 and 2)
 *   D  latch 9234D678, 05h 12022923, then 00000000
 *   E  latch FFFFFFFF, 02h 7FF12345, 03h 7FF10C21, 04h 7FF336AB, then 00000000
 *   F  00000000, then 00000000
 * psx-spx's "MSBs = Nothing" read as "unchanged" would keep bit 31 as well in
 * D and E (92022923, FFF12345...).
 *
 * CONSOLE DAY (2026-10-05/06): the SCPH-5501, 7501 and 9001, byte for byte
 * alike - DIAG's rule and psx-spx's, bit 31 included:
 *   A  latch 80008000, then 80008000 80008000 80008000
 *   B  latch 12345678, then 12345678
 *   C  00000002, then 00000002 00000002
 *   D  latch 9234D678, 05h 92022923, then 92022923
 *   E  latch FFFFFFFF, 02h FFF12345, 03h FFF10C21, 04h FFF336AB, then FFF336AB
 *   F  00000000, then 00000000
 * ON THIS CORE SINCE v0.377.18 the same, every row; amidog's REG screen stays
 * green under it, and nocash DIAG's GPU INFO line went OK.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <gpu.h>
#include <io.h>

#define GPUSTAT (*(volatile uint32_t*)0x1F801814)
#define GPUREAD (*(volatile uint32_t*)0x1F801810)

static void settle() { for (volatile int i = 0; i < 64; i++) { } }

/*
 * Prime the latch with `w` through GP0(C0h): four pixels at 1000,500 (off any
 * display) read back as TWO words, `w` first, then GPUREAD read once more -
 * the latch itself, which is what this returns. Two words, not one: once a
 * VRAM read is over the latch holds its FIRST word if it had two or more, and
 * the value from before the read if it had one (amidog psxtest_gpu FIFO >
 * READ, M223). This test's first version primed with one word and its prime
 * never stuck.
 */
static uint32_t vramWord(uint32_t w) {
    vramPut(1000, 500, w & 0xFFFF);
    vramPut(1001, 500, w >> 16);
    vramPut(1002, 500, 0x1111);
    vramPut(1003, 500, 0x2222);
    DrawSync(0);
    VRAM2CPU buf;
    setcode(&buf, 0xC0);
    setlen(&buf, 3);
    buf.x0 = 1000; buf.y0 = 500; buf.w = 4; buf.h = 1;
    DrawPrim(&buf);
    while (!(GPUSTAT & (1 << 27))) { }
    (void)GPUREAD;
    (void)GPUREAD;
    settle();
    return GPUREAD;
}

static uint32_t info(int index) {
    writeGP1(0x10, index);
    settle();
    return GPUREAD;
}

static void row(const char* what, uint32_t v) { printf("    %-28s %08x\n", what, (unsigned)v); }

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpuinfo-latch\n");
    printf("GPUREAD after GP1(10h), primed by a VRAM read or by a GP1 answer.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    writeGP0(0xE2, 0x12345);                          /* texture window: 20 bits */
    writeGP0(0xE3, (0x043 << 10) | 0x021);            /* drawing area top-left */
    writeGP0(0xE4, (0x0CD << 10) | 0x2AB);            /* bottom-right */
    writeGP0(0xE5, (0x045 << 11) | 0x123);            /* offset: 22 bits */
    DrawSync(0);
    EnterCriticalSection();

    printf("  A  VRAM read, then unused indices\n");
    row("VRAM read 80008000, latch", vramWord(0x80008000));
    row("GP1(10h) 00h", info(0x00));
    row("GP1(10h) 06h", info(0x06));
    row("GP1(10h) 0Fh", info(0x0F));
    printf("  B\n");
    row("VRAM read 12345678, latch", vramWord(0x12345678));
    row("GP1(10h) 00h", info(0x00));
    printf("  C  a GP1 answer, then unused indices\n");
    row("GP1(10h) 07h (GPU type)", info(0x07));
    row("GP1(10h) 00h", info(0x00));
    row("GP1(10h) 06h", info(0x06));
    printf("  D\n");
    row("VRAM read 9234D678, latch", vramWord(0x9234D678));
    row("GP1(10h) 05h (offset)", info(0x05));
    row("GP1(10h) 00h", info(0x00));
    printf("  E\n");
    row("VRAM read FFFFFFFF, latch", vramWord(0xFFFFFFFF));
    row("GP1(10h) 02h (tex window)", info(0x02));
    row("GP1(10h) 03h (area TL)", info(0x03));
    row("GP1(10h) 04h (area BR)", info(0x04));
    row("GP1(10h) 00h", info(0x00));
    printf("  F\n");
    row("GP1(10h) 08h", info(0x08));
    row("GP1(10h) 00h", info(0x00));

    ExitCriticalSection();
    printf("\n  amidog's rule: an unused index keeps only bit 31. DIAG's: it keeps everything.\n");
    printf("\ngpu/gpuinfo-latch: Done.\n");
    for (;;) VSync(0);
}
