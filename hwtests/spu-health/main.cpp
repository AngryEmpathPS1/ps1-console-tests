/*
 * spu/spu-health - does the SPU still play a voice, and does switching it off and on bring it back? (2026-10-08, the
 * SCPH-7501 left running overnight under the test runner.)
 *
 * WHY. On the 7501 the runner's first batch ran spu-latch with voice 1's onset at 8 (the 5501's), and later in the same
 * batch spu-decode-ahead found IRQ9 once and never again, spu-latch's second run found no onset at all (65535 on all 98
 * rows) and spu-lead no onset and no IRQ9 - while the 5501 ran the same programs in the same order the night before with
 * none of it. The runner keys every voice off, zeroes their registers and sound RAM, and puts SPUCNT and the transfer
 * control back between tests; it never switches the SPU off. Something the SPU keeps survived all of that.
 *
 * WHAT IT DOES. Interrupts off. Prints SPUCNT, SPUSTAT, the transfer control, the main volume, ENDX, DPCR and DMA4's
 * CHCR as found. Then the voice: voice 1 at full volume, pitch 1000h, sound RAM 1000h, the fastest attack and the
 * highest sustain (ADSR1 00FFh), keyed on, and its envelope (ENVX) and current volume read at 0, 2,000, 8,000 and 30,000
 * cycles - a playing voice is at 7FFFh within a few samples - then keyed off. Then the SPU switched OFF (SPUCNT 0, its
 * status bits waited for), 100,000 cycles, ON (SPUCNT C000h: on, unmuted), 100,000 cycles, the voice again. SPUCNT is
 * left on and unmuted (the runner puts back what it found before the next test).
 *
 * THE 7501's FIRST ANSWER (2026-10-08 night): voice 1 PLAYS as found and after the off/on - and SPUCNT as found is C085h,
 * the reverb's master enable among its bits (the virtual console's Unirom leaves 0000h), which the runner puts back after
 * every test while the reverb's own settings stay as a test left them. So, second: the reverb registers as found (mBASE
 * and 1DC0h-1DFEh), and SOUND RAM WATCHED - a pattern written by DMA4 at 1000h, 40000h and 7E000h (2 KB each), 300,000
 * cycles with SPUCNT as found, read back, the words that changed counted; then the same with the reverb bit cleared.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-health --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define V1(off)  (0x1F801C10u + (off))
#define SPU(off) (0x1F801D80u + (off))
#define SPUCNT   0x1F801DAAu
#define SPUSTAT  0x1F801DAEu
#define DPCR     0x1F8010F0u
#define CHCR4    0x1F8010C8u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitc(uint32_t n) { const uint32_t t0 = now(); while (now() - t0 < n) {} }

#define DPCR_A 0x1F8010F0u
static uint16_t cntv;
static void spuMode(int m) {
    cntv = (uint16_t)((read16(SPUCNT) & ~0x30) | (m << 4));
    write16(SPUCNT, cntv);
    for (int i = 0; i < 100000 && (read16(SPUSTAT) & 0x3F) != (cntv & 0x3F); i++) {}
}
static uint32_t buf[512] __attribute__((aligned(16)));
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    const uint32_t dpcr = read32(DPCR_A);
    write32(DPCR_A, dpcr | 0x00080000u);
    spuMode(0);
    write16(0x1F801DA6u, (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    spuMode(0);
    write32(DPCR_A, dpcr);
}
static const uint32_t WATCH[3] = { 0x1000, 0x40000, 0x7E000 };
static void watch(const char* label) {
    for (int w = 0; w < 3; w++) { for (int i = 0; i < 512; i++) buf[i] = 0xA5000000u | (WATCH[w] + i * 4); spuDma(WATCH[w], buf, 512, true); }
    waitc(300000);
    printf("  %-22s SPUCNT %04x  changed:", label, read16(SPUCNT));
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < 512; i++) buf[i] = 0;
        spuDma(WATCH[w], buf, 512, false);
        /* a DMA4 read comes back late by a halfword or two (the virtual console: the first halfword junk, the rest one on),
         * so the pattern is matched at the shift that fits best, halfword by halfword */
        const uint16_t* r = (const uint16_t*)buf;
        int best = 2000, bestShift = 0, first = -1;
        for (int sh = 0; sh <= 2; sh++) {
            int changed = 0, fi = -1;
            for (int k = 0; k + sh < 1024; k++) {
                const uint32_t word = 0xA5000000u | (WATCH[w] + (k >> 1) * 4);
                const uint16_t want = (k & 1) ? (uint16_t)(word >> 16) : (uint16_t)word;
                if (r[k + sh] != want) { changed++; if (fi < 0) fi = k; }
            }
            if (changed < best) { best = changed; bestShift = sh; first = fi; }
        }
        printf("  %05x: %4d of 1024 halfwords (read %d late)", (unsigned)WATCH[w], best, bestShift);
        if (first >= 0) printf(" first at +%x", first * 2);
    }
    printf("\n");
}
static void reverb() {
    printf("  reverb as found: mBASE %04x (sound RAM %05x), volumes %04x %04x, IN %04x %04x, regs 1DC0h:", read16(0x1F801DA2u),
           (unsigned)read16(0x1F801DA2u) * 8, read16(0x1F801D84u), read16(0x1F801D86u), read16(0x1F801DFCu), read16(0x1F801DFEu));
    for (uint32_t a = 0x1F801DC0u; a < 0x1F801DFCu; a += 2) printf(" %04x", read16(a));
    printf("\n");
}
static void regs(const char* when) {
    printf("  %-22s SPUCNT %04x SPUSTAT %04x XFER %04x MAINVOL %04x %04x ENDX %04x%04x DPCR %08x CHCR4 %08x\n", when,
           read16(SPUCNT), read16(SPUSTAT), read16(SPU(0x2C)), read16(SPU(0x00)), read16(SPU(0x02)),
           read16(SPU(0x1E)), read16(SPU(0x1C)), (unsigned)read32(DPCR), (unsigned)read32(CHCR4));
}

static void voice(const char* label) {
    write16(V1(0x0), 0x3FFF); write16(V1(0x2), 0x3FFF);    /* volume L R */
    write16(V1(0x4), 0x1000);                              /* pitch: 44,100 Hz */
    write16(V1(0x6), 0x1000 / 8);                          /* start: sound RAM 1000h */
    write16(V1(0x8), 0x00FF);                              /* ADSR1: fastest attack, decay shift 15, sustain level max */
    write16(V1(0xA), 0x0000);                              /* ADSR2 */
    write16(V1(0xC), 0x0000);                              /* ENVX */
    write16(V1(0xE), 0x1000 / 8);                          /* repeat */
    write16(SPU(0x08), 0x0002); write16(SPU(0x0A), 0x0000);   /* KON voice 1 */
    const uint32_t t0 = now();
    static const uint32_t at[4] = { 0, 2000, 8000, 30000 };
    uint16_t env[4], cur[4];
    for (int i = 0; i < 4; i++) { while (now() - t0 < at[i]) {} env[i] = read16(V1(0xC)); cur[i] = read16(0x1F801E04u); }
    printf("  %-22s ENVX %04x %04x %04x %04x  current volume %04x %04x %04x %04x  -> %s\n", label,
           env[0], env[1], env[2], env[3], cur[0], cur[1], cur[2], cur[3], env[3] >= 0x4000 ? "PLAYS" : "SILENT");
    write16(SPU(0x0C), 0x0002); write16(SPU(0x0E), 0x0000);   /* KOFF */
    waitc(20000);
}

int main() {
    printf("\nspu/spu-health\n");
    printf("Does voice 1 play (its envelope after key-on), as found and after the SPU is switched off and on. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    regs("as found:");
    reverb();
    if (!(read16(SPUCNT) & 0x8000)) { write16(SPUCNT, 0xC000); waitc(100000); }   /* the transfers need the SPU on (the virtual console finds it off) */
    const uint16_t cnt0 = read16(SPUCNT);
    watch("sound RAM as found:");
    write16(SPUCNT, cnt0 & ~0x0080u);
    watch("the reverb bit off:");
    write16(SPUCNT, cnt0);
    voice("voice 1 as found:");
    regs("after it:");
    write16(SPUCNT, 0x0000);
    { const uint32_t t0 = now(); while ((read16(SPUSTAT) & 0x3F) && now() - t0 < 1000000u) {} }
    waitc(100000);
    regs("switched off:");
    write16(SPUCNT, 0xC000);
    waitc(100000);
    regs("switched on:");
    voice("voice 1 after it:");
    regs("at the end:");
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-health: Done.\n");
    for (;;) VSync(0);
}
