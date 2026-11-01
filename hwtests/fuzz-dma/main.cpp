/*
 * dma/fuzz-dma - random DMA transfers, timed and read back: channels, sizes, modes, chopping, priorities, nobody's chosen cases (2026-11-01)
 *
 * WHY. The next of the random tests, after the CPU, GTE, GPU, SPU and MDEC. Every DMA test here so far picks its sizes and asks one question. A transfer that ends has more to say than its time: MADR and BCR
 * as it left them, CHCR, DICR's flags, I_STAT, and the words themselves. A core that gets the time right by a wrong route shows up in those.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock. Each of 2,048 ids is one transfer from xorshift32 seeded 0x9E3779B9 * (id + 1), so a line that differs can be run alone: OTC of 1..512; GPU to VRAM and
 * back in blocks (sync 1); a GPU linked list of 1..16 entries of nops and fills (sync 2); SPU RAM written and read back in blocks; SPU RAM by burst (sync 0) with chopping, random windows. DPCR's priorities are random. The
 * start is CHCR's write, the end is a C loop polling CHCR's busy bit, so the time is the transfer plus that loop's phase; the loop is the same on every core, and a timeout clears the channel and says so.
 *
 * READ IT LIKE THIS. D id kind chcr bcr time polls madr bcr' chcr' dicr' istat hash: the channel's registers after it ended, I_STAT's DMA bit, FNV over the 1,024 words of the buffer. There are no assertions -
 * compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-dma --frames=20000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0      0x1F801810u
#define GP1      0x1F801814u
#define I_STAT   0x1F801070u
#define DPCR     0x1F8010F0u
#define DICR     0x1F8010F4u
#define SPU_ADDR 0x1F801DA6u
#define SPU_CTRL 0x1F801DACu
#define SPUCNT   0x1F801DAAu
#define SPUSTAT  0x1F801DAEu
#define MADR(c)  (0x1F801080u + (c) * 16u)
#define BCR(c)   (0x1F801084u + (c) * 16u)
#define CHCR(c)  (0x1F801088u + (c) * 16u)
#define IDS 2048
#define WORDS 1024
#define POLLS 200000

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t buf[WORDS + 64] __attribute__((aligned(16)));
static int timeouts;

static void gpuReady() { for (int i = 0; i < 400000; i++) if ((read32(GP1) & 0x14000000u) == 0x14000000u) return; timeouts++; printf("TIMEOUT gpu %x\n", read32(GP1)); }
static void spuMode(uint32_t mode) {
    write16(SPUCNT, (uint16_t)(0xC000u | (mode << 4)));
    for (int i = 0; i < 4000; i++) if ((read16(SPUSTAT) & 0x30u) == (mode << 4)) return;
    timeouts++; printf("TIMEOUT spu %x\n", read16(SPUSTAT));
}
static uint32_t hash() { uint32_t h = 2166136261u; for (int i = 0; i < WORDS; i++) { h ^= buf[i]; h *= 16777619u; } return h; }

int main() {
    printf("\ndma/fuzz-dma\n");
    printf("%d ids: D id kind chcr bcr time polls madr bcr' chcr' dicr' istat hash. No assertions.\n\n", IDS);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    write16(SPU_CTRL, 4);
    for (int id = 0; id < IDS; id++) {
        rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
        for (int i = 0; i < WORDS; i++) buf[i] = rnd();
        uint32_t dp = 0;
        for (int c = 0; c < 7; c++) dp |= (rnd() & 7u) << (c * 4);
        dp |= (1u << 11) | (1u << 19) | (1u << 27);
        write32(DPCR, dp);
        const int kind = (int)(rnd() % 7);
        int ch = 6;
        uint32_t madr = 0, bcr = 0, chcr = 0;
        switch (kind) {
            case 0: {                                   /* OTC */
                const uint32_t n = 1 + rnd() % 512, off = rnd() % 64;
                madr = (uint32_t)&buf[off + n - 1] & 0xFFFFFCu; bcr = n; chcr = 0x11000002u; ch = 6;
                break;
            }
            case 1: case 2: {                           /* GPU to VRAM, and back */
                const uint32_t bs = 1 + rnd() % 16, ba = 1 + rnd() % 16, w = bs * ba * 2, y = 256 + rnd() % 200, x = rnd() % 256;
                gpuReady();
                write32(GP1, 0x04000000u | (kind == 1 ? 2u : 3u));
                write32(GP0, kind == 1 ? 0xA0000000u : 0xC0000000u);
                write32(GP0, (y << 16) | x);
                write32(GP0, (1u << 16) | w);
                madr = (uint32_t)&buf[rnd() % 64] & 0xFFFFFCu; bcr = (ba << 16) | bs; ch = 2;
                chcr = kind == 1 ? 0x01000201u : 0x01000200u;
                if (kind == 2) for (int i = 0; i < 40000; i++) if (read32(GP1) & (1u << 27)) break;
                break;
            }
            case 3: {                                   /* a GPU linked list */
                const uint32_t m = 1 + rnd() % 16;
                uint32_t p = 0;
                gpuReady();
                write32(GP1, 0x04000002u);
                madr = (uint32_t)&buf[0] & 0xFFFFFCu;
                for (uint32_t e = 0; e < m; e++) {
                    const uint32_t n = rnd() % 9;
                    const uint32_t nx = p + 1 + n;
                    buf[p] = (n << 24) | (e + 1 == m ? 0xFFFFFFu : ((uint32_t)&buf[nx] & 0xFFFFFCu));
                    for (uint32_t i = 0; i < n; i++) buf[p + 1 + i] = 0;
                    p = nx;
                }
                bcr = 0; ch = 2; chcr = 0x01000401u;
                break;
            }
            case 4: case 5: {                           /* SPU RAM in blocks */
                const uint32_t bs = 1 + rnd() % 16, ba = 1 + rnd() % 16;
                spuMode(0);
                write16(SPU_ADDR, (uint16_t)(0x200u + rnd() % 0x800u));
                spuMode(kind == 4 ? 2 : 3);
                madr = (uint32_t)&buf[rnd() % 64] & 0xFFFFFCu; bcr = (ba << 16) | bs; ch = 4;
                chcr = kind == 4 ? 0x01000201u : 0x01000200u;
                break;
            }
            default: {                                  /* SPU RAM by burst, chopped */
                const uint32_t n = 1 + rnd() % 256;
                spuMode(0);
                write16(SPU_ADDR, (uint16_t)(0x200u + rnd() % 0x800u));
                spuMode(2);
                madr = (uint32_t)&buf[rnd() % 64] & 0xFFFFFCu; bcr = n; ch = 4;
                chcr = 0x11000101u | ((rnd() & 7u) << 16) | ((rnd() & 7u) << 20);
                break;
            }
        }
        write32(DICR, 0x7F800000u | (1u << (16 + ch)));
        write32(I_STAT, ~8u);
        write32(MADR(ch), madr);
        write32(BCR(ch), bcr);
        const uint16_t t0 = read16(T2_COUNT);
        write32(CHCR(ch), chcr);
        uint32_t polls = 0;
        while ((read32(CHCR(ch)) & 0x01000000u) && polls < POLLS) polls++;
        const uint16_t t1 = read16(T2_COUNT);
        const uint32_t ma = read32(MADR(ch)), bc = read32(BCR(ch)), cc = read32(CHCR(ch)), di = read32(DICR), is = read32(I_STAT) & 8u;
        if (polls >= POLLS) { timeouts++; write32(CHCR(ch), 0); }
        printf("D %d %d %x %x %u %u %x %x %x %x %x %x\n", id, kind, chcr, bcr, (uint16_t)(t1 - t0), polls, ma & 0xFFFFFFu, bc, cc, di, is, hash());
        write32(DICR, 0x7F000000u);
        write32(I_STAT, ~8u);
    }
    spuMode(0);
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write32(DICR, di0 & 0x00FFFFFFu);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ndma/fuzz-dma: Done.\n", timeouts);
    for (;;) VSync(0);
}
