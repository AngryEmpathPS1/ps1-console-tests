/*
 * cdrom/cd-ident - WHAT DISC IS THIS (2026-11-22, AngryEmpath through main: an untested disc in the SCPH-5501-u3, named hands-free).
 * Not a test. hwtests/cd-discid's Init / GetTN / GetTD (its waits, copied), then three sectors read off the disc: sector 16 (the ISO
 * primary volume descriptor: its volume ID), the root directory's first sector (from the PVD's root record), and SYSTEM.CNF's
 * first sector, whose BOOT= line names the executable (SLUS_xxx.xx - the game's code). A drive that will not spin up or read says so.
 * Interrupts off; every wait bounded (CLAUDE.md console rule 1; flag reads as psx-spx "Unstable IRQ Flag polling", take()).
 * Prints:
 *   ident-toc: stat SS tracks F-L lead-out MM:SS:FF starts MM:SS ...      | ident-toc: no disc (...)
 *   ident-pvd: id 'CD001' system 'PLAYSTATION' vol '<volume id>' root lba N  | ident-pvd: not read (...)
 *   ident-dir: SYSTEM.CNF lba N size N   (and every root entry's name)       | ident-dir: ...not found / not read
 *   ident-cnf: <each line of SYSTEM.CNF>                                     | ident-cnf: not read
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t p) { idx(0); *CD2 = p; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static uint8_t rb[16]; static int nrb;
static uint8_t take(uint32_t limit) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    nrb = 0; while (nrb < 16 && hasResp()) rb[nrb++] = resp();
    drain();
    ackAll();
    return f;
}
static uint8_t cmd(uint8_t c, const uint8_t *p, int np, bool second, uint8_t *f2) {
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    uint8_t f; const uint32_t t0 = now();
    do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000));
    if (second) { uint8_t g = 0; if (f == 3) { do { g = take(MS(10000)); } while (g == 1 && now() - t0 < MS(10000)); } if (f2) *f2 = g; }
    return f;
}
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* One 2048-byte sector by its LBA: Setloc (LBA + 150 frames as MSF), ReadN to its first INT1, the sector through the data FIFO,
 * Pause. Returns 1 when read, else 0 with *why set (the flag that came instead: 0 = nothing in time, 5 = an error). */
static uint8_t sec[2048];
static int readSector1(uint32_t lba, uint8_t *why) {
    const uint32_t a = lba + 150;
    const uint8_t loc[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) };
    cmd(0x02, loc, 3, false, 0);
    int ok = 0; uint8_t g = 0;
    if (cmd(0x06, 0, 0, false, 0) == 3) {
        const uint32_t t0 = now();
        do { g = take(MS(10000)); } while (g != 1 && g != 5 && g != 0 && now() - t0 < MS(10000));
        if (g == 1) {
            idx(0); *CD3 = 0x00; *CD3 = 0x80;
            const uint32_t t1 = now(); while (!(*CD0 & 0x40) && now() - t1 < MS(100)) {}
            for (int i = 0; i < 2048; i++) sec[i] = *CD2;
            idx(0); *CD3 = 0x00;
            ok = 1;
        }
    } else g = 0xFF;
    uint8_t p2 = 0; cmd(0x09, 0, 0, true, &p2);
    if (why) *why = g;
    return ok;
}
/* three tries: an untested drive that is slow to find its first sector gets a fair chance */
static int tries3 = 0;
static int readSector(uint32_t lba, uint8_t *why) { for (int i = 0; i < 3; i++) { tries3++; if (readSector1(lba, why)) return 1; } return 0; }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void text(const uint8_t *p, int n, char *out) {   /* printable, trailing spaces cut */
    int m = 0; for (int i = 0; i < n; i++) out[m++] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '?';
    while (m && out[m - 1] == ' ') m--; out[m] = 0;
}

int main() {
    printf("\ncdrom/cd-ident\nWhat disc is this: the TOC, the volume ID, SYSTEM.CNF's BOOT line.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    uint8_t f2 = 0;
    const uint8_t fi = cmd(0x0A, 0, 0, true, &f2);          /* Init to its INT2 */
    const uint8_t st = nrb ? rb[0] : 0;
    uint8_t gs = st; int polls = 0;
    for (const uint32_t t0 = now(); now() - t0 < MS(15000); polls++) {
        if (cmd(0x01, 0, 0, false, 0) == 3 && nrb) gs = rb[0];
        if ((gs & 0x02) && !(gs & 0xE0)) break;
        idle(MS(50));
    }
    uint8_t tn[3] = { 0, 0, 0 }; int ntn = 0;
    uint8_t ft = 0, e5[2] = { 0, 0 }; int tries = 0;
    for (; tries < 20; tries++) {
        ft = cmd(0x13, 0, 0, false, 0);
        if (ft == 3) break;
        if (ft == 5) { e5[0] = nrb > 0 ? rb[0] : 0; e5[1] = nrb > 1 ? rb[1] : 0; }
        idle(MS(500));
    }
    for (int i = 0; i < 3 && i < nrb; i++) tn[i] = rb[i]; ntn = nrb;
    const uint8_t z[1] = { 0 };
    const uint8_t fl = cmd(0x14, z, 1, false, 0);
    uint8_t lo[3] = { 0, 0, 0 }; for (int i = 0; i < 3 && i < nrb; i++) lo[i] = rb[i];
    int first = unbcd(tn[1]), last = unbcd(tn[2]);
    static uint8_t starts[100][2];
    int ns = 0;
    if (ft == 3 && fl == 3 && ntn >= 3 && first >= 1 && last >= first && last <= 99)
        for (int t = first; t <= last && ns < 100; t++) {
            const uint8_t p[1] = { bcd(t) };
            if (cmd(0x14, p, 1, false, 0) == 3 && nrb >= 3) { starts[ns][0] = rb[1]; starts[ns][1] = rb[2]; ns++; }
        }
    /* the three sectors, kept as text until interrupts are back on */
    static char pvdLine[200], dirLine[600], cnf[700];
    pvdLine[0] = dirLine[0] = cnf[0] = 0;
    int pvdOk = 0, cnfOk = 0; uint32_t cnfLba = 0, cnfSize = 0; uint8_t why = 0;
    if (ft == 3 && fl == 3) {
        const uint8_t m0[1] = { 0x00 }; cmd(0x0E, m0, 1, false, 0);   /* Setmode 0: 2048-byte sectors, 1X */
        if (readSector(16, &why)) {
            char id[8], sys[40], vol[40]; text(sec + 1, 5, id); text(sec + 8, 32, sys); text(sec + 40, 32, vol);
            const uint32_t root = le32(sec + 156 + 2), rootSize = le32(sec + 156 + 10);
            sprintf(pvdLine, "ident-pvd: type %u id '%s' system '%s' vol '%s' root lba %u size %u", sec[0], id, sys, vol, (unsigned)root, (unsigned)rootSize);
            pvdOk = 1;
            if (root && readSector(root, &why)) {
                int n = sprintf(dirLine, "ident-dir: root:");
                for (int o = 0; o < 2048 && n < 560; ) {
                    const int len = sec[o]; if (!len) break;
                    const int nl = sec[o + 32];
                    char name[40]; int m = 0; for (int i = 0; i < nl && m < 38; i++) { const uint8_t c = sec[o + 33 + i]; name[m++] = c < 0x20 ? (c ? '.' : '~') : (char)c; } name[m] = 0;
                    if (o >= 68) n += sprintf(dirLine + n, " %s", name);     /* the first two are "." and ".." */
                    if (nl >= 10 && sec[o + 33] == 'S' && sec[o + 34] == 'Y' && sec[o + 35] == 'S' && sec[o + 36] == 'T' && sec[o + 39] == '.' && sec[o + 40] == 'C')
                        { cnfLba = le32(sec + o + 2); cnfSize = le32(sec + o + 10); }
                    o += len;
                }
                if (cnfLba && readSector(cnfLba, &why)) {
                    int m = 0; const int lim = cnfSize < 600 ? (int)cnfSize : 600;
                    for (int i = 0; i < lim; i++) { const uint8_t c = sec[i]; cnf[m++] = (c == '\r') ? ' ' : (c == '\n' || (c >= 0x20 && c < 0x7F)) ? (char)c : '?'; }
                    cnf[m] = 0; cnfOk = 1;
                }
            } else if (!dirLine[0]) sprintf(dirLine, "ident-dir: root not read (flag %u)", why);
        } else sprintf(pvdLine, "ident-pvd: not read (sector 16, 3 tries: last flag %u - 0 nothing in 10 s, 5 an error)", why);
    }
    ExitCriticalSection();
    printf("ident-wait: Getstat %02X after %d polls; GetTN tries %d", gs, polls, tries + 1);
    if (ft != 3) printf(", last INT5 %02X %02X", e5[0], e5[1]);
    printf("\n");
    printf("ident-init: INT%u stat %02X, second INT%u; GetTN INT%u %02X %02X %02X; GetTD INT%u %02X %02X %02X; sector reads tried %d\n", fi, st, f2, ft, tn[0], tn[1], tn[2], fl, lo[0], lo[1], lo[2], tries3);
    if (ft != 3 || fl != 3 || ntn < 3)
        printf("ident-toc: no disc (Init INT%u stat %02X, second INT%u; GetTN INT%u; GetTD INT%u)\n", fi, st, f2, ft, fl);
    else {
        printf("ident-toc: stat %02X tracks %d-%d lead-out %02d:%02d starts", lo[0], first, last, unbcd(lo[1]), unbcd(lo[2]));
        for (int i = 0; i < ns; i++) printf(" %02d:%02d", unbcd(starts[i][0]), unbcd(starts[i][1]));
        printf("\n");
    }
    printf("%s\n", pvdLine[0] ? pvdLine : "ident-pvd: not read (no TOC)");
    if (dirLine[0]) printf("%s\n", dirLine);
    if (pvdOk) printf(cnfLba ? "ident-dir: SYSTEM.CNF lba %u size %u\n" : "ident-dir: no SYSTEM.CNF in the root's first sector\n", (unsigned)cnfLba, (unsigned)cnfSize);
    if (cnfOk) { for (char *s = cnf; *s; ) { char *e = s; while (*e && *e != '\n') e++; const char c = *e; *e = 0; if (*s) printf("ident-cnf: %s\n", s); if (!c) break; s = e + 1; } }
    else if (cnfLba) printf("ident-cnf: not read (flag %u)\n", why);
    printf("cdrom/cd-ident: Done.\n");
    return 0;
}
