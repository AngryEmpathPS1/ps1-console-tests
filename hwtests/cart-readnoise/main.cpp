/*
 * cart-readnoise (2026-11-25, the cart session): HOW NOISY IS A CART'S ROM AT EACH EXPANSION 1 READ DELAY. The SCPH-1000's GameShark 2.3 (port 3475) answers
 * the same address differently from read to read (a 9-read vote: 98.24% of the 256 KB unanimous, up to 0.8% of one read's bytes off the vote), which a voted dump can
 * average away but a write test cannot trust. For each EXP1 delay value, this reads the first 32 KB of the cart window (1F000000h) 4 times, then counts the bytes
 * that differ from read 0, and the bytes that differ between ALL four: if the noise is the ROM being read too fast, the count falls to 0 at a slow enough delay.
 * Reads the cart only; writes nothing but the EXP1 delay register (restored afterwards).
 */
#include <common.h>
#include <psxapi.h>
#include <stdio.h>
#define EXP1_DELAY (*(volatile uint32_t *)0x1F801008u)
#define I_MASK     (*(volatile uint32_t *)0x1F801074u)
static uint8_t rd[4][0x8000];
int main() {
    printf("\ncart-readnoise\n");
    const uint32_t e1 = EXP1_DELAY, im = I_MASK;
    printf("EXP1 delay on entry %08X\n", (unsigned)e1);
    const uint32_t delays[] = { e1, 0x0013243Fu, 0x0013240Fu, 0x0013241Fu, 0x0013242Fu, 0x0013200Fu, 0x0013000Fu, 0x0013243Du, 0x0013243Eu, 0x0013253Fu, 0x0013263Fu, 0x0013343Fu, 0x0013643Fu };
    for (unsigned d = 0; d < sizeof delays / sizeof delays[0]; d++) {
        I_MASK = 0; EXP1_DELAY = delays[d];
        for (int r = 0; r < 4; r++) { volatile uint8_t *p = (volatile uint8_t *)0xBF000000u; for (int i = 0; i < 0x8000; i++) rd[r][i] = p[i]; }
        EXP1_DELAY = e1; I_MASK = im;
        unsigned d1 = 0, d2 = 0, d3 = 0, any = 0;
        for (int i = 0; i < 0x8000; i++) {
            const bool a = rd[1][i] != rd[0][i], b = rd[2][i] != rd[0][i], c = rd[3][i] != rd[0][i];
            d1 += a; d2 += b; d3 += c; any += a || b || c || rd[2][i] != rd[1][i] || rd[3][i] != rd[1][i] || rd[3][i] != rd[2][i];
        }
        unsigned hist[8][2] = {};   // per bit: differs from read 0 as 1->0 (cart read 1 first, later 0) and 0->1
        for (int r = 1; r < 4; r++) for (int i = 0; i < 0x8000; i++) { const uint8_t x = rd[r][i] ^ rd[0][i]; for (int b = 0; b < 8; b++) if (x >> b & 1) hist[b][(rd[0][i] >> b) & 1]++; }
        printf("  bit flips vs read 0 [bit: read0 was 1 / read0 was 0]:");
        for (int b = 0; b < 8; b++) printf(" %d:%u/%u", b, hist[b][1], hist[b][0]);
        printf("\n");
        printf("delay %08X: bytes off read 0: read1 %u read2 %u read3 %u; bytes not unanimous %u of 32768\n", (unsigned)delays[d], d1, d2, d3, any);
    }
    printf("Done.\n");
    for (;;) {}
}
