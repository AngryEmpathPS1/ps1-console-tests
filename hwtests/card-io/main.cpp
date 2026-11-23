/*
 * card-io - READS AND WRITES A MEMORY CARD IN SLOT 1 OR 2, run by the resident runner (owner AngryEmpath, 2026-11-21: tools a program can be, so the console
 * never has to boot into Unirom for them). The runner is untouched; tools/cardio.js loads this exe (its orders patched), and for a write the image as a
 * data-only exe, runs it, reads the log, and fetches the dump with the runner's M.
 *   op 1  the card's FLAG, its ID pair and the Get-ID answer, nothing else (writes nothing)
 *   op 2  READ the whole card (1,024 frames of 128 bytes, retried up to 3 times each) into DUMP (800E0000h); prints the dump's sha1
 *   op 3  WRITE the image at IMG (800C0000h, 128 KB): the image must hash, ON THE CONSOLE, to the sha1 in the orders (the owner-approved one). The card is read
 *         first, only the frames that DIFFER are written (3 tries each; no dummy write to frame 3Fh, so a card's wear is only what the image needs), then the whole
 *         card is read again (VER, 80100000h) and must equal the image
 * THE PROTOCOL is psx-spx "Memory Card Read/Write Commands" and what hwtests/card-fetch measured on the consoles: SIO0 by hand at 250 kHz (JOY_BAUD 88h), interrupts
 * off, each byte written then its /ACK waited for (the Sony cards take ~31,000 cycles after the read command's 7th byte); a read is 81h 52h 00h 00h MSB LSB 00h 00h
 * 00h 00h, 128 data bytes, the checksum (MSB xor LSB xor data) and the end byte 47h; a write 81h 57h 00h 00h MSB LSB, 128 data bytes, the checksum, 00h 00h 00h with
 * 5Ch 5Dh and the end byte (47h good, 4Eh bad checksum, FFh bad sector) coming back. Every wait is bounded. The port is left as the runner's restoreKernel leaves
 * every test's (the runner restores SIO0 after the program's "Done.").
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>
#include "../cart-flash/sha1.h"

#define JOY_DATA 0x1F801040
#define JOY_STAT 0x1F801044
#define JOY_MODE 0x1F801048
#define JOY_CTRL 0x1F80104A
#define JOY_BAUD 0x1F80104E
#define IMG 0x800C0000u
#define DUMP 0x800E0000u
#define VER 0x80100000u
#define LIMIT 600000

struct Orders {
    uint32_t magic;      /* 'AECD' 44434541h */
    uint32_t op;
    uint32_t slot;       /* 0 or 1 */
    uint32_t length;     /* 20000h for a write */
    uint32_t flags;
    uint8_t  sha1[20];
    uint32_t endMagic;   /* 'ENDC' 43444E45h */
};
__attribute__((used)) volatile struct Orders orders = { 0x44434541u, 0, 0, 0, 0, { 0 }, 0x43444E45u };

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(uint32_t cycles) { const uint16_t t0 = t2(); uint32_t e = 0; uint16_t last = t0; while (e < cycles) { const uint16_t n = t2(); e += (uint16_t)(n - last); last = n; } }

static uint32_t SLOT;

/* One byte out, its reply back (-1: none), then its /ACK (low, then high) unless it is the last. The ACK is looked for by the level and by the sticky IRQ bit
 * (JOY_CTRL bit 12 arms it), so a short pulse between two polls is still seen. */
static int sendByte(uint8_t tx, bool last) {
    while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
    write16(JOY_CTRL, 0x1013 | (SLOT << 13));                         /* acknowledge the old flags, select, ACK interrupt armed (the flag only: interrupts are off) */
    write8(JOY_DATA, tx);
    uint32_t e = 0; uint16_t lastT = t2();
    int reply = -1;
    bool sawAck = false;
    for (;;) {
        const uint32_t st = read32(JOY_STAT);
        const uint16_t n = t2(); e += (uint16_t)(n - lastT); lastT = n;
        if (reply < 0 && (st & 2)) reply = read8(JOY_DATA);
        if ((st & 0x80) || (st & 0x200)) sawAck = true;
        if (reply >= 0 && (last || (sawAck && !(st & 0x80)))) break;
        if (e > LIMIT) break;
    }
    if (reply < 0 && (read32(JOY_STAT) & 2)) reply = read8(JOY_DATA);
    if (!last && !sawAck) return -1;                                   /* no /ACK: the card is not answering */
    return reply;
}

/* A transaction; returns 0 on success, else 1 + the index of the byte that failed (no reply or /ACK) or 200 + a reason. flag/idA/idB: the first replies. */
static uint32_t gFlag, gId1, gId2, gEnd, gConf;
static int transact(char cmd, uint32_t frame, uint8_t *data) {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003 | (SLOT << 13));
    wait(1500);
    const uint8_t msb = (uint8_t)(frame >> 8), lsb = (uint8_t)frame;
    int r, b = 0;
#define STEP(tx, last) do { r = sendByte((tx), (last)); if (r < 0) { write16(JOY_CTRL, 0); return 1 + b; } b++; } while (0)
    STEP(0x81, false);
    r = sendByte(cmd == 'R' ? 0x52 : cmd == 'W' ? 0x57 : 0x53, false); if (r < 0) { write16(JOY_CTRL, 0); return 2; } gFlag = r; b = 2;
    r = sendByte(0, false); if (r < 0) { write16(JOY_CTRL, 0); return 3; } gId1 = r; b = 3;
    r = sendByte(0, false); if (r < 0) { write16(JOY_CTRL, 0); return 4; } gId2 = r; b = 4;
    int rc = 0;
    if (gId1 != 0x5A || gId2 != 0x5D) { rc = 250; goto out; }
    if (cmd == 'S') {                                                  /* 5Ch 5Dh 04h 00h 00h 80h */
        uint8_t g[6];
        for (int i = 0; i < 6; i++) { r = sendByte(0, i == 5); if (r < 0) { rc = 5 + i; goto out; } g[i] = (uint8_t)r; }
        gConf = (g[2] << 24) | (g[3] << 16) | (g[4] << 8) | g[5];
        goto out;
    }
    if (cmd == 'R') {
        STEP(msb, false); STEP(lsb, false);
        r = sendByte(0, false); if (r < 0) { rc = 7; goto out; }       /* 5Ch */
        r = sendByte(0, false); if (r < 0) { rc = 8; goto out; }       /* 5Dh, the late /ACK follows */
        r = sendByte(0, false); if (r < 0) { rc = 9; goto out; } const int cm = r;
        r = sendByte(0, false); if (r < 0) { rc = 10; goto out; } const int cl = r;
        if (cm != msb || cl != lsb) { rc = 251; goto out; }             /* FFFFh: a sector the card refuses */
        uint8_t x = msb ^ lsb;
        for (int i = 0; i < 128; i++) { r = sendByte(0, false); if (r < 0) { rc = 11 + i; goto out; } data[i] = (uint8_t)r; x ^= (uint8_t)r; }
        r = sendByte(0, false); if (r < 0) { rc = 139; goto out; }
        const int chk = r;
        r = sendByte(0, true); if (r < 0) { rc = 140; goto out; }
        gEnd = r;
        if (gEnd != 0x47) rc = 252; else if (chk != x) rc = 253;
        goto out;
    }
    /* 'W' */
    {
        STEP(msb, false); STEP(lsb, false);
        uint8_t x = msb ^ lsb;
        for (int i = 0; i < 128; i++) { r = sendByte(data[i], false); if (r < 0) { rc = 7 + i; goto out; } x ^= data[i]; }
        r = sendByte(x, false); if (r < 0) { rc = 135; goto out; }
        r = sendByte(0, false); if (r < 0) { rc = 136; goto out; }     /* 5Ch */
        r = sendByte(0, false); if (r < 0) { rc = 137; goto out; }     /* 5Dh */
        r = sendByte(0, true); if (r < 0) { rc = 138; goto out; }
        gEnd = r;
        if (gEnd != 0x47) rc = 254;
    }
out:
    write16(JOY_CTRL, 0x0000);
    return rc;
}

struct Stats { uint32_t good, bad, firstBad, retries, lastErr; };
/* The whole card into `dst`; 3 tries per frame. */
static void readCard(uint8_t *dst, Stats &s) {
    s.good = s.bad = s.retries = 0; s.firstBad = 0xFFFFFFFFu; s.lastErr = 0;
    for (uint32_t f = 0; f < 1024; f++) {
        int rc = 1;
        for (int t = 0; t < 3 && rc; t++) { rc = transact('R', f, dst + f * 128); if (rc && t < 2) s.retries++; }
        if (rc) { s.bad++; s.lastErr = rc; if (s.firstBad == 0xFFFFFFFFu) s.firstBad = f; for (int i = 0; i < 128; i++) dst[f * 128 + i] = 0xEE; }
        else s.good++;
    }
}
static void showSha(const char *what, const uint8_t *p, uint32_t n) {
    uint8_t h[20]; sha1(p, n, h);
    printf("%s=", what); for (int i = 0; i < 20; i++) printf("%02x", h[i]); printf("\n");
}
static uint32_t getSr() { uint32_t v; __asm__ volatile("mfc0 %0, $12\n\tnop" : "=r"(v)); return v; }
static void setSr(uint32_t v) { __asm__ volatile("mtc0 %0, $12\n\tnop" :: "r"(v)); }

int main() {
    printf("\ncard-io\n");
    const uint32_t op = orders.op, len = orders.length;
    SLOT = orders.slot;
    if (orders.magic != 0x44434541u || orders.endMagic != 0x43444E45u || op < 1 || op > 3 || SLOT > 1 || (op == 3 && len != 0x20000)) { printf("status=0 (REFUSED: the orders)\nDone.\n"); return 0; }
    uint32_t status = 1;
    if (op == 3) {
        uint8_t h[20]; sha1((const uint8_t *)IMG, len, h);
        printf("sha1=");
        for (int i = 0; i < 20; i++) printf("%02x", h[i]);
        printf("\n");
        for (int i = 0; i < 20; i++) if (h[i] != orders.sha1[i]) { printf("status=4 (REFUSED: the image that arrived is not the approved sha1; nothing written)\nDone.\n"); return 0; }
    }
    const uint32_t sr = getSr(); setSr(sr & ~1u);
    /* the card must answer */
    int rc = transact('S', 0, 0);
    printf("slot=%u flag=%02x id=%02x%02x getid=%08x rc=%d\n", SLOT + 1, gFlag, gId1, gId2, gConf, rc);
    if (rc) { status = 2; }
    else if (op == 2) {
        Stats s; readCard((uint8_t *)DUMP, s);
        printf("read: frames good=%u bad=%u firstbad=%d retries=%u lasterr=%u\n", s.good, s.bad, (int)s.firstBad, s.retries, s.lastErr);
        showSha("dump-sha1", (const uint8_t *)DUMP, 0x20000);
        if (s.bad) status = 3;
    } else if (op == 3) {
        Stats s; readCard((uint8_t *)DUMP, s);
        printf("before: good=%u bad=%u\n", s.good, s.bad);
        showSha("before-sha1", (const uint8_t *)DUMP, 0x20000);
        uint32_t differ = 0, written = 0, retried = 0, wbad = 0, nrc = 0, rcs[8], ends[8], frs[8];
        if (s.bad) status = 3;                                         /* a card that will not read is not written */
        else {
            for (uint32_t f = 0; f < 1024; f++) {
                const uint8_t *a = (const uint8_t *)IMG + f * 128, *b = (const uint8_t *)DUMP + f * 128;
                bool d = false; for (int i = 0; i < 128; i++) if (a[i] != b[i]) { d = true; break; }
                if (!d) continue;
                differ++;
                int w = 1;
                for (int t = 0; t < 3 && w; t++) {
                    w = transact('W', f, (uint8_t *)a);
                    if (w && t < 2) { retried++; if (nrc < 8) { rcs[nrc] = w; ends[nrc] = gEnd; frs[nrc++] = f; } }
                }
                if (w) { wbad++; status = 5; printf("write failed at frame %u: rc=%d end=%02x\n", f, w, gEnd); break; }
                written++;
                /* THE CARD'S OWN WRITE TIME (2026-11-21, the fuzzer's FreePSXBoot writes on the consoles: the 9001 retried 980 of 981 frames, the 7001 999 of 1000, a 5-frame write 3 of 5, a 1-frame
                 * write none: the FIRST frame of a run goes through and the next ones fail their first try, so a real card is probably still busy committing the frame just written when the next
                 * command arrives - the emulator's cards are never busy, which is why every rehearsal was clean). The pause before the next command is the orders' `flags` in microseconds (the PC's
                 * --wdelay=); the retry reasons below say what the failed tries saw. */
                if (orders.flags) wait(orders.flags * 34);
            }
            printf("write: frames differing=%u written=%u retried=%u failed=%u wdelay=%uus\n", differ, written, retried, wbad, orders.flags);
            for (uint32_t i = 0; i < nrc; i++) printf("  retry %u: frame %u rc=%u end=%02x\n", i, frs[i], rcs[i], ends[i]);
            Stats v; readCard((uint8_t *)VER, v);
            uint32_t diff = 0, first = 0xFFFFFFFFu;
            for (uint32_t i = 0; i < 0x20000; i++) if (((const uint8_t *)VER)[i] != ((const uint8_t *)IMG)[i]) { if (first == 0xFFFFFFFFu) first = i; diff++; }
            printf("verify: reread good=%u bad=%u; bytes differing from the image=%u first=%d\n", v.good, v.bad, diff, (int)first);
            showSha("after-sha1", (const uint8_t *)VER, 0x20000);
            if (!wbad && (diff || v.bad)) status = 3;
        }
    }
    setSr(sr);
    write32(0x1F801070, ~(1u << 7));                                    /* IRQ7's flag in I_STAT (the ACK flag armed above) */
    printf("status=%u\nDone.\n", status);
    return 0;
}
