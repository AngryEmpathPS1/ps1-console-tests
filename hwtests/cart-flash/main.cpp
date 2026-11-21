/*
 * cart-flash - WRITES AN IMAGE TO A DATEL CART'S FLASH, run by the resident runner (owner AngryEmpath, 2026-11-21: "Cart doesn't work? Immediate
 * reflash" - so a cart that is dead can be written from the runner's L/R, with no boot into Unirom). The runner stays as it is; this is a
 * program it loads and runs like any test (tools/cartflash.js --runner builds it, patches its orders, sends it and the image, reads the log).
 *
 * THE IMAGE is not in this exe: it is a second, data-only exe (tools/cartflash.js dataExe) the runner loads to IMG (800C0000h, 256 KB at most;
 * the runner refuses anything that reaches 80130000h). THE ORDERS are this exe's own `orders` block, found by its magic and patched by the PC
 * before the load: op, length, flags and the SHA-1 the OWNER approved (cartflash --sha1, the value AngryEmpath said GO to). The chip is
 * written only if the image as it arrived HERE hashes to that value (the runner's own check is a byte sum), so what is written is the
 * approved image or nothing.
 *   op 1  the chip's ID, nothing else      op 2  write the image (sectors / pages that differ), then verify all of it
 *   op 3  verify only (the chip against the image)
 * WHAT IT PRINTS (the PC reads it with G): `id`, `sha1` (of the image, as received), `status` (0 REFUSED orders, 1 done, 2 REFUSED chip ID,
 * 3 a page did not verify, 4 timeout, 5 REFUSED sha1) with the counts, then "Done.". The flash code is cartrom/flasher.c's, unchanged in
 * what it sends the chip (psx-spx "Cheat Devices - FLASH/EEPROMs"; the AM29F010 and SST29EE020 command sets, measured on the consoles
 * 2026-11-16 / 17 - see that file's header): it runs from RAM with interrupts off from the first chip command to the last, and the runner
 * restores the machine (restoreKernel) after the program's "Done." as after every test.
 */
#include <stdint.h>
#include <stdio.h>

#define R8(a) (*(volatile uint8_t *)(a))
#define IMG 0x800C0000u
#define BASE0 0x1F000000u

struct Orders {
    uint32_t magic;      /* 'AEFX' 58464541h */
    uint32_t op;
    uint32_t length;     /* bytes of the image, a multiple of 128, at most 40000h (20000h on an AM29F010) */
    uint32_t flags;      /* bit 0: accept the emulator's SST29EE010 (BFh 07h) */
    uint8_t  sha1[20];   /* the approved image's */
    uint32_t endMagic;   /* 'ENDX' 58444E45h */
};
/* in .data, found by the PC by magic; volatile so nothing here is folded to its compile-time values */
__attribute__((used)) volatile struct Orders orders = { 0x58464541u, 0, 0, 0, { 0 }, 0x58444E45u };

static struct { uint32_t status, id, written, same, bad, firstBad, retries, erased; } A;

#include "sha1.h"

/* ---- the chip: cartrom/flasher.c's code ---- */
static uint32_t base_of(uint32_t off) { return off < 0x20000 ? BASE0 : 0x1F040000u; }
static uint32_t addr_of(uint32_t off) { return base_of(off) + (off & 0x1FFFF); }
static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) {} }
static void cmd(uint32_t base, uint8_t c) { R8(base + 0x5555) = 0xAA; R8(base + 0x2AAA) = 0x55; R8(base + 0x5555) = c; }
static int wait_ready(uint32_t a, uint8_t data) {
    spin(100);
    uint32_t n;
    for (n = 0; R8(a) != R8(a); n++) if (n > 200000) return 0;
    for (n = 0; R8(a) != data; n++) if (n > 200000) return 0;
    return 1;
}
static uint32_t read_id(uint32_t base) {
    cmd(base, 0x90); spin(100);
    const uint32_t id = R8(base) | (R8(base + 1) << 8);
    cmd(base, 0xF0); spin(100);
    return id;
}
static int page_equal(uint32_t off, const volatile uint8_t *s) {
    const uint32_t a = addr_of(off);
    for (uint32_t i = 0; i < 128; i++) if (R8(a + i) != s[i]) return 0;
    return 1;
}
static int write_page(uint32_t off, const volatile uint8_t *s) {
    const uint32_t a = addr_of(off);
    cmd(base_of(off), 0xA0);
    for (uint32_t i = 0; i < 128; i++) R8(a + i) = s[i];
    if (!wait_ready(a + 127, s[127])) return -1;
    return page_equal(off, s);
}
static int amd_wait(uint32_t a, uint8_t data, uint32_t limit) {
    for (uint32_t n = 0; n < limit; n++) {
        const uint8_t v = R8(a);
        if (((v ^ data) & 0x80) == 0 && R8(a) == data) return 1;
        if (v & 0x20) { if (R8(a) == data) return 1; R8(BASE0) = 0xF0; return 0; }
    }
    R8(BASE0) = 0xF0;
    return 0;
}
static int amd_sector(uint32_t off, const volatile uint8_t *s, uint32_t len, int *wrote) {
    int need_erase = 0, differs = 0;
    for (uint32_t i = 0; i < len; i++) { const uint8_t c = R8(BASE0 + off + i); if (c != s[i]) differs = 1; if (s[i] & ~c) need_erase = 1; }
    if (!differs) return 1;
    if (need_erase) {
        cmd(BASE0, 0x80); R8(BASE0 + 0x5555) = 0xAA; R8(BASE0 + 0x2AAA) = 0x55; R8(BASE0 + off) = 0x30;
        spin(100);
        if (!amd_wait(BASE0 + off, 0xFF, 40000000u)) return -1;
        A.erased++;
    }
    for (uint32_t i = 0; i < len; i++) {
        const uint32_t a = BASE0 + off + i;
        if (R8(a) == s[i]) continue;
        cmd(BASE0, 0xA0); R8(a) = s[i];
        if (!amd_wait(a, s[i], 200000)) return -1;
        wrote[i >> 7] = 1;
    }
    for (uint32_t i = 0; i < len; i++) if (R8(BASE0 + off + i) != s[i]) return 0;
    return 1;
}

static void flasher(uint32_t op, uint32_t len, uint32_t flags) {
    const volatile uint8_t *src = (const volatile uint8_t *)IMG;
    A.firstBad = 0xFFFFFFFFu;
    const uint32_t id0 = read_id(BASE0);
    const int amd = id0 == 0x2001;
    A.id = id0 | ((amd ? 0xFFFFu : read_id(0x1F040000u)) << 16);
    if (op == 1) { A.status = 1; return; }
    if (!(id0 == 0x10BF || amd || ((flags & 1) && id0 == 0x07BF))) { A.status = 2; return; }
    if (amd && len > 0x20000) { A.status = 2; return; }
    if (op == 2 && amd) {
        for (uint32_t off = 0; off < len; off += 0x4000) {
            const uint32_t n = len - off < 0x4000 ? len - off : 0x4000;
            int wrote[128];
            for (uint32_t p = 0; p < 128; p++) wrote[p] = 0;
            int r = amd_sector(off, src + off, n, wrote);
            if (r == 0) { A.retries++; r = amd_sector(off, src + off, n, wrote); }
            if (r < 0) { A.status = 4; A.firstBad = off; return; }
            if (r == 0) { A.status = 3; A.firstBad = off; return; }
            for (uint32_t p = 0; p < n / 128; p++) { if (wrote[p]) A.written++; else A.same++; }
        }
    } else if (op == 2) {
        for (uint32_t off = 0; off < len; off += 128) {
            if (page_equal(off, src + off)) { A.same++; continue; }
            int r = write_page(off, src + off);
            if (r != 1) { A.retries++; r = write_page(off, src + off); }
            if (r < 0) { A.status = 4; A.firstBad = off; return; }
            if (r == 0) { A.status = 3; A.firstBad = off; return; }
            A.written++;
        }
    }
    for (uint32_t off = 0; off < len; off += 128) {
        const uint32_t a = addr_of(off);
        for (uint32_t i = 0; i < 128; i++) if (R8(a + i) != src[off + i]) { if (A.firstBad == 0xFFFFFFFFu) A.firstBad = off + i; A.bad++; break; }
    }
    A.status = A.bad ? 3 : 1;
}

static uint32_t getSr() { uint32_t v; __asm__ volatile("mfc0 %0, $12\n\tnop" : "=r"(v)); return v; }
static void setSr(uint32_t v) { __asm__ volatile("mtc0 %0, $12\n\tnop" :: "r"(v)); }

int main() {
    printf("\ncart-flash\n");
    const uint32_t op = orders.op, len = orders.length, flags = orders.flags;
    if (orders.magic != 0x58464541u || orders.endMagic != 0x58444E45u || op < 1 || op > 3 || (len & 127) || len > 0x40000 || (op != 1 && !len)) {
        printf("status=0 (REFUSED: the orders)\nDone.\n");
        return 0;
    }
    uint8_t h[20];
    if (op != 1) {
        sha1((const uint8_t *)IMG, len, h);
        printf("sha1=");
        for (int i = 0; i < 20; i++) printf("%02x", h[i]);
        printf("\n");
        int same = 1;
        for (int i = 0; i < 20; i++) if (h[i] != orders.sha1[i]) same = 0;
        if (!same) { printf("status=5 (REFUSED: the image that arrived is not the approved sha1; nothing written)\nDone.\n"); return 0; }
    }
    const uint32_t sr = getSr();
    setSr(sr & ~1u);                    /* interrupts off from the first chip command to the last */
    flasher(op, len, flags);
    setSr(sr);
    printf("id=%04x/%04x\n", A.id & 0xFFFF, A.id >> 16);
    printf("status=%u written=%u same=%u retries=%u erased=%u bad=%u firstbad=%08x\n", A.status, A.written, A.same, A.retries, A.erased, A.bad, A.firstBad);
    printf("Done.\n");
    return 0;
}
