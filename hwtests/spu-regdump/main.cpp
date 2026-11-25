/*
 * spu/spu-regdump - EVERY SPU REGISTER AS A TEST FINDS IT (2026-11-25, the SPU session)
 *
 * WHY. hwtests/spu-capring4: on four consoles no capture ring raised IRQ9 though all four rings were being written, on six all four
 * rings did - and the two SCPH-1001s were one on each side, so it is state a program left, not the hardware. Which register holds it?
 * A dump from a console on each side, compared, says - or says it is in none that can be read.
 *
 * WHAT IT DOES. Reads only (nothing is written, no interrupt is touched): 1F801C00h-1F801E7Fh as halfwords, twice 100 ms apart
 * (a register that moves by itself - ENVX, the current volumes, SPUSTAT - shows as two values).
 *   D addr v0 .. v15          sixteen halfwords from addr, the first pass
 *   X addr first second       a halfword that read differently on the second pass
 *   B f8010f0 f801014 f801020 DPCR, the SPU's delay / size, COM_DELAY
 * THE RULE: run here first: `node hwtests/run.js spu-regdump --ntsc --frames=600 --quiet=300`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

int main() {
    static uint16_t a[320], b[320];
    EnterCriticalSection();
    for (int i = 0; i < 320; i++) a[i] = read16(0x1F801C00u + 2u * (uint32_t)i);
    for (volatile int w = 0; w < 300000; w++) {}
    for (int i = 0; i < 320; i++) b[i] = read16(0x1F801C00u + 2u * (uint32_t)i);
    const uint32_t dpcr = read32(0x1F8010F0), sd = read32(0x1F801014), cd = read32(0x1F801020);
    ExitCriticalSection();
    printf("\nspu/spu-regdump\nEvery SPU register as found: D addr sixteen halfwords; X addr first second (moved between two passes); B DPCR SPU-delay COM_DELAY. No assertions.\n\n");
    for (int r = 0; r < 20; r++) {
        printf("D %03x", 0xC00 + r * 32);
        for (int i = 0; i < 16; i++) printf(" %04x", a[r * 16 + i]);
        printf("\n");
    }
    for (int i = 0; i < 320; i++) if (a[i] != b[i]) printf("X %03x %04x %04x\n", 0xC00 + 2 * i, a[i], b[i]);
    printf("B %08x %08x %08x\n", (unsigned)dpcr, (unsigned)sd, (unsigned)cd);
    printf("\nspu/spu-regdump: Done.\n");
    for (;;) VSync(0);
}
