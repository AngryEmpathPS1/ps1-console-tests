/*
 * machine-id - what the console says about itself, before anything is measured.
 *
 * WHY (2026-10-02; an AI's console notes, not included). tools/consolegrade.js
 * grades every printout against THIS emulator running as an SCPH-9001: its BIOS
 * 4.1 A (scph7001.bin, CRC 502224B6) and, with the drive's own program, the CD
 * controller dated 99/02/01 (CDROM-firmware/SC430944.S19). A console with another
 * BIOS revision or another controller version would make every difference after
 * it a question of which machine, not of the model. So the day's first program
 * prints what this one is, and changes nothing:
 *   - the BIOS's version string (at BFC7FF32h, where the page reads it, M292) and
 *     a CRC32 over the whole 512 KB ROM (the zip CRC, as the BIOS dumps are named);
 *   - the CD controller's version: Test 19h,20h, psx-spx "INT3(yy,mm,dd,ver)";
 *   - its region: Test 19h,22h, an ASCII string ("for U/C"), vC1 and up (INT5 on
 *     vC0, psx-spx's caution);
 *   - the GPU's type: GP1(10h) with 07h, then GPUREAD - psx-spx's "GPU Versions":
 *     2 on the new 208-pin GPU; the old 160-pin one leaves GPUREAD as it was;
 *   - RAM_SIZE (1F801060h) as the BIOS left it.
 * The only writes: the CD's index, parameter and command registers, its flag
 * acknowledges, and GP1(10h), which only latches a value into GPUREAD. CD
 * interrupts are masked and polled (the BIOS's own handler would take them), as
 * the cd-* tests do, and put back to all five enabled at the end.
 *
 * THE RULE: run it here first (`node hwtests/run.js machine-id`). ON THIS CORE
 * (2026-10-02, v0.306). scph1001.bin and the CD model: BIOS "System ROM Version
 * 2.2 12/04/95 A", CRC32 37157331 (that dump's well-known CRC, so the instrument
 * reads the whole ROM right); CD controller 95/05/16 version C1, region "for U/C";
 * GPU type 00000002 (GPUREAD 0 before it); RAM_SIZE 00000B88. As the SCPH-9001
 * (PSX_BIOS=scph7001.bin, --ntsc, PSX_CD_FIRMWARE=CDROM-firmware/SC430944.S19):
 * "System ROM Version 4.1 12/16/97 A", CRC32 502224B6, the controller 99/02/01
 * version C3, "for U/C", the rest the same. consolegrade runs it on the model, so
 * its controller line differs from a 9001's by construction: that line is the
 * reading, not a fault.
 *
 * SINCE 2026-10-08, FOR THE SCPH-1001: the CPU's PRID (cop0r15: 1 on the
 * CXD8530, the launch boards' CPU, 2 on the CXD8606 - psx-spx) and the drive's
 * chip strings, Test 19h,23h-25h (INT5 on a vC0 drive). Here (v0.377.46): PRID
 * 00000002 on every model (the 1001's 1 is a placeholder not yet in the core),
 * the chips "CXD2940Q" x3 on the default model, INT5 03 10 on the 1001's vC0.
 * And RAM_SIZE's top half is the main bus's last word (src/psx.js busWord): this
 * build reads 80010B88h here, where console day's build read 24840B88h on all
 * three consoles (an I-cache fill's last word, 24840EF4h) - a prediction.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }

#define GPUSTAT (*(volatile uint32_t *)0x1F801814)
#define GPUREAD (*(volatile uint32_t *)0x1F801810)
#define RAM_SIZE (*(volatile uint32_t *)0x1F801060)

/* The next CD interrupt (a bounded wait: a spin, not a timer, so nothing else is
 * touched): its number, 0 if none came, with the response bytes in r[]. */
static uint8_t r[17];
static int n;
static uint8_t next() {
    uint8_t f = 0;
    for (uint32_t k = 0; k < 20000000 && !(f = irqFlag()); k++) { }
    n = 0;
    while (f && hasResp() && n < 16) r[n++] = resp();
    ackAll();
    return f;
}

/* CRC32 (the zip polynomial, reflected), a byte at a time with a table. */
static uint32_t table[256];
static uint32_t crc32(const volatile uint8_t *p, uint32_t len) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

int main() {
    printf("\nsystem/machine-id\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();                     /* CD interrupts masked: polled */

    /* The BIOS. */
    const volatile uint8_t *rom = (const volatile uint8_t *)0xBFC00000;
    char ver[80];
    int k = 0;
    for (; k < 78 && rom[0x7FF32 + k] >= 0x20 && rom[0x7FF32 + k] < 0x7F; k++) ver[k] = rom[0x7FF32 + k];
    ver[k] = 0;
    printf("BIOS: \"%s\"\n", k ? ver : "(no version string at BFC7FF32h)");
    printf("BIOS CRC32: %08X\n", crc32(rom, 512 * 1024));

    /* The CD controller. */
    param(0x20); command(0x19);
    uint8_t f = next();
    if (f == 3 && n >= 4) printf("CD controller (19h,20h): %02x/%02x/%02x version %02X\n", r[0], r[1], r[2], r[3]);
    else { printf("CD controller (19h,20h): INT%d", f); for (int i = 0; i < n; i++) printf(" %02X", r[i]); printf("\n"); }
    param(0x22); command(0x19);
    f = next();
    if (f == 3 && n) { r[n] = 0; printf("CD region (19h,22h): \"%s\"\n", (const char *)r); }
    else { printf("CD region (19h,22h): INT%d", f); for (int i = 0; i < n; i++) printf(" %02X", r[i]); printf("\n"); }

    /* The GPU. GPUREAD is read once first, so an old GPU's unchanged answer shows as unchanged. */
    uint32_t before = GPUREAD;
    GPUSTAT = 0x10000007;                           /* GP1(10h:07h): the GPU type */
    for (volatile int w = 0; w < 64; w++) { }
    uint32_t type = GPUREAD;
    printf("GPU type (GP1(10h:07h)): %08X (GPUREAD before it %08X)\n", type, before);

    printf("RAM_SIZE (1F801060h): %08X\n", RAM_SIZE);

    /* The CPU (2026-10-08, for the SCPH-1001): cop0r15, psx-spx "PRID=00000001h on Playstation with CPU
     * CXD8530BQ/CXD8530CQ, PRID=00000002h on Playstation with CPU CXD8606CQ" - the launch boards' CPU against the
     * PU-18-and-later one. Read-only. */
    uint32_t prid;
    __asm__ volatile("mfc0 %0, $15" : "=r"(prid));
    printf("CPU PRID (cop0r15): %08X\n", prid);

    /* The drive's chip strings (2026-10-08): Test 19h,23h / 24h / 25h - psx-spx "Supported only in BIOS version vC1
     * and up. Not supported in vC0", so a launch PU-7's drive answers INT5. Each console's own program says which
     * chips it was written for (CONSOLE_MODELS drive.ids). */
    for (uint8_t sub = 0x23; sub <= 0x25; sub++) {
        param(sub); command(0x19);
        f = next();
        if (f == 3 && n) { r[n] = 0; printf("CD chips (19h,%02xh): \"%s\"\n", sub, (const char *)r); }
        else { printf("CD chips (19h,%02xh): INT%d", sub, f); for (int i = 0; i < n; i++) printf(" %02X", r[i]); printf("\n"); }
    }

    idx(1); *CD2 = 0x1F;                            /* the CD's interrupts as the BIOS had them */
    ackAll();
    ExitCriticalSection();
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
