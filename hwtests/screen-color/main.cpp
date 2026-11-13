/*
 * screen-color - NOT A TEST: paints the whole screen one colour for a few seconds, so a console on a capture card shows it is
 * alive and which one it is (2026-11-13: the live views were black, and nothing showed whether a console was running).
 * The colour is picked by the console's region from its BIOS (the PAL 7502 red, an NTSC console blue), so each card's picture
 * names its console. Display reset to 320x240 15-bit, VRAM filled with GP0(02h), display on, ~8 s on Timer 2, then the display
 * goes back off and "Done." prints. Nothing else touched; every runner test sets its own GPU state.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitGpu() { for (int i = 0; i < 100000 && !(read32(GP1) & 0x04000000u); i++) {} }

int main() {
    // the BIOS's version string at BFC7FF32h ("System ROM Version 4.1 12/16/97 E", v2.x on): its last letter is the region,
    // A / J / E. Older BIOSes have none and read as NTSC - only the colour depends on it.
    const volatile char *r = (const volatile char *)0xBFC7FF32;
    bool pal = false;
    if (r[0] == 'S' && r[1] == 'y' && r[2] == 's') { int i = 0; while (i < 0x40 && r[i]) i++; pal = i > 0 && r[i - 1] == 'E'; }
    const uint32_t color = pal ? 0x0000E0u /* red */ : 0xE04000u /* blue */;
    printf("\nscreen-color: %s, painting %s for 8 s (not a test)\n", pal ? "PAL BIOS" : "NTSC BIOS", pal ? "red" : "blue");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    write32(GP1, 0x00000000);                       // reset
    write32(GP1, 0x08000001 | (pal ? 0x08 : 0));    // 320 wide, 240 lines, 15-bit, NTSC/PAL
    write32(GP1, 0x06000000 | (0x260 + 320 * 8) << 12 | 0x260);
    write32(GP1, 0x07000000 | (pal ? ((0xA3 + 256) << 10 | 0xA3) : ((0x88 - 120 + 240) << 10 | (0x88 - 120))));
    write32(GP1, 0x05000000);                       // display area at 0,0
    waitGpu();
    write32(GP0, 0x02000000 | color);               // fill rect
    write32(GP0, 0x00000000);                       // x 0, y 0
    write32(GP0, (512u << 16) | 1008u);             // 1008 x 512 (fill widths are 16-aligned)
    waitGpu();
    write32(GP1, 0x03000000);                       // display on
    const uint32_t t0 = now();
    while (now() - t0 < 8u * 33868800u) {}
    write32(GP1, 0x03000001);                       // display off again
    ExitCriticalSection();
    printf("Done.\n");
    return 0;
}
