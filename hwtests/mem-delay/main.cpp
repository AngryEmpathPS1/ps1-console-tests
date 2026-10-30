/*
 * cpu/mem-delay — does COM0 change an access time, and by how much?
 *
 * WHY THIS TEST EXISTS. STATUS M129 ended with a named experiment and no way to
 * run it. The Delay/Size registers (1F801008h..1Ch) and COM_DELAY (1F801020h)
 * set how long an access to BIOS, Expansion, the CDROM and the SPU takes. This
 * core STORES them and ignores them, returning fixed per-region constants - and
 * M129 measured that this is NOT a tidy hole nothing falls into:
 *
 *   ff7    CD_DELAY 0x20843 x1,    0x20943 x1130  COM_DELAY 0x132c x206, 0x1325 x941, 0x1323 x924
 *   re-dc  CD_DELAY 0x20843 x1897, 0x20943 x4681  COM_DELAY 0x132c x338, 0x1325 x4355, 0x1323 x4343
 *
 * Every game rewrites them thousands of times a boot, and what they change is
 * always the same two things: the CDROM register's "Use COM0" bit
 * (0x20843 <-> 0x20943) and COM0 itself, which moves between 3, 5 and 12.
 *
 * psx-spx publishes a formula, and M129 found it EXACT for every region that
 * does not use COM0 and wrong for both that do - its rule makes SEQ smaller
 * than 1ST, where hardware's own numbers have SPU at 18/21 and EXPANSION2 at
 * 11/15, the sequential access SLOWER. Implementing the published rule would
 * swing CD access cost 57% thousands of times a boot on a rule the log shows
 * to be wrong. M129 refused, and wrote down the run that would settle it:
 *
 *   "cpu/access-time runs under exactly ONE COM_DELAY (the BIOS's 0x31125) and
 *    one CDROM register (0x20843), while games use at least four COM_DELAY
 *    values and both CDROM values. Running that same test with COM_DELAY set to
 *    0x1323, 0x1325 and 0x132c, and with CD_DELAY at 0x20943, would give the
 *    COM0 dependence directly - six more runs of a test that already exists."
 *
 * This is those runs, as one program: it sets each combination, times the
 * access, and puts them in one table so the COM0 term can be read straight off
 * the differences.
 *
 * SAFETY. Every value written here is one a retail game writes on a real
 * console, and the original contents are saved first and restored after every
 * row as well as at the end. Nothing here touches a region the program runs
 * from: RAM and BIOS timing are left exactly as the kernel set them.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js mem-delay`):
 *   rows DIFFER by setting since M230 (the slow regions follow their registers, nocash's formula):
 *   CD 8/16/32-bit 8.07/14.07/26.07 at the kernel's values, 7.95/25.95/61.95 at CD=20943 COM=0132C;
 *   SPU 18.05/18.05/39.07 at the kernel's values, 45.95 32-bit at COM=0132C.
 * ANSWERED by the SCPH-5501, 7501 and 9001 (console day, all alike; v0.377.35, src/psx.js slowAccessB2B): COM0 (bits 8
 * and 0) costs COM0 - 1 on each extra bus cycle and max(0, COM0 - 6) once an access - CD=20943 13.79 / 30.79 / 64.79
 * at COM=0132C, 7.85 / 17.85 / 37.85 at 01325; the SPU 32-bit 38.04 at the kernel's values. AT v0.377.35 every row
 * within 0.11 (its baseline loop 414 here, 419 on the consoles).
 */
#include <common.h>
#include <measure.hpp>

#define COM_DELAY  0x1f801020
#define CD_DELAY   0x1f801018
#define SPU_DELAY  0x1f801014

#define COUNT 100

static inline uint32_t rd(uint32_t a) { return *(volatile uint32_t*)a; }
static inline void wr(uint32_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

/*
 * The same baseline-subtracted loop cpu/access-time uses, but with the address
 * built at assembly time so no register setup lands inside the timed span.
 */
#define MEASURE_READ(WIDTH, ADDR)                                            \
    measure<MeasureMethod::CpuClock>([] {                                    \
        asm volatile(".set push\n.set noreorder\n"                           \
                     "lui  $t0, %[hi]\n"                                     \
                     "ori  $t0, $t0, %[lo]\n"                                \
                     "li   $t2, %[n]\n"                                      \
                     "1:\n"                                                  \
                     "nop\n"                                                 \
                     WIDTH " $t1, 0($t0)\n"                                  \
                     "addiu $t2, $t2, -1\n"                                  \
                     "bnez  $t2, 1b\n"                                       \
                     "nop\n"                                                 \
                     ".set pop\n"                                            \
                     :: [hi] "i"((ADDR) >> 16), [lo] "i"((ADDR) & 0xffff),   \
                        [n] "i"(COUNT) : "t0", "t1", "t2", "memory");        \
    })

#define MEASURE_NOP()                                                        \
    measure<MeasureMethod::CpuClock>([] {                                    \
        asm volatile(".set push\n.set noreorder\n"                           \
                     "li   $t2, %[n]\n"                                      \
                     "1:\n"                                                  \
                     "nop\n"                                                 \
                     "addiu $t2, $t2, -1\n"                                  \
                     "bnez  $t2, 1b\n"                                       \
                     "nop\n"                                                 \
                     ".set pop\n"                                            \
                     :: [n] "i"(COUNT) : "t2", "memory");                    \
    })

static void cell(uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("%3u.%02u  ", d / COUNT, d % COUNT);
}

int main() {
    printf("\ncpu/mem-delay\n");
    printf("Access time to CDROM and SPU under each COM_DELAY / Use-COM0 combination\n");
    printf("games actually write. Baseline-subtracted, %d iterations, cycles per access.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");

    const uint32_t savedCom = rd(COM_DELAY);
    const uint32_t savedCd  = rd(CD_DELAY);
    const uint32_t savedSpu = rd(SPU_DELAY);
    printf("  as the kernel left them: COM_DELAY=%08x CD_DELAY=%08x SPU_DELAY=%08x\n\n",
           savedCom, savedCd, savedSpu);

    const uint32_t base = MEASURE_NOP();
    printf("  baseline loop: %u cycles for %d iterations\n\n", base, COUNT);

    /* The values M129 counted in real boots, plus the BIOS's own. */
    static const uint32_t COMS[] = { 0x31125, 0x1323, 0x1325, 0x132c };
    static const uint32_t CDS[]  = { 0x20843, 0x20943 };

    printf("CDROM_STAT (1F801800h)             8bit   16bit   32bit\n");
    for (unsigned ci = 0; ci < sizeof(CDS) / sizeof(CDS[0]); ci++) {
        for (unsigned mi = 0; mi < sizeof(COMS) / sizeof(COMS[0]); mi++) {
            wr(COM_DELAY, COMS[mi]);
            wr(CD_DELAY, CDS[ci]);
            /* "Use COM0" is bit 8 of the Delay/Size register. */
            printf("  CD=%05x COM=%05x %s  ", CDS[ci], COMS[mi], (CDS[ci] & 0x100) ? "[COM0]" : "      ");
            cell(MEASURE_READ("lbu", 0x1f801800), base);
            cell(MEASURE_READ("lhu", 0x1f801800), base);
            cell(MEASURE_READ("lw",  0x1f801800), base);
            printf("\n");
            wr(CD_DELAY, savedCd);
            wr(COM_DELAY, savedCom);
        }
    }

    printf("\nSPU (1F801DAAh)                    8bit   16bit   32bit\n");
    for (unsigned mi = 0; mi < sizeof(COMS) / sizeof(COMS[0]); mi++) {
        wr(COM_DELAY, COMS[mi]);
        printf("  SPU=%05x COM=%05x %s  ", savedSpu, COMS[mi], (savedSpu & 0x100) ? "[COM0]" : "      ");
        cell(MEASURE_READ("lbu", 0x1f801daa), base);
        cell(MEASURE_READ("lhu", 0x1f801daa), base);
        cell(MEASURE_READ("lw",  0x1f801da8), base);
        printf("\n");
        wr(COM_DELAY, savedCom);
    }

    wr(COM_DELAY, savedCom);
    wr(CD_DELAY, savedCd);
    wr(SPU_DELAY, savedSpu);
    printf("\n  restored: COM_DELAY=%08x CD_DELAY=%08x SPU_DELAY=%08x\n",
           rd(COM_DELAY), rd(CD_DELAY), rd(SPU_DELAY));

    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
