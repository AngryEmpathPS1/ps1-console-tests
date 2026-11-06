/*
 * cpu/irq-unmask3 - irq-unmask / irq-unmask2: after the store that unmasks a pending VBlank the console runs 1-4 more
 * instructions before the interrupt, repeatably per variant but with no cost pattern across them - a phase, not a delay? Each
 * variant reads I_STAT (an I/O read: the write buffer empty), then 0 or 2 RAM stores (W0 / W2), then K nops (0-15), then stores
 * I_MASK = 1 and counts in t5: where the interrupt lands, for every K. A constant N means a delay of N; N + K stepping with a
 * period means a free-running clock the request waits for. 2026-11-06, Claude (AI) for AngryEmpathPS1.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070
#define I_MASK 0x1F801074
#define LIMIT 2000000

extern "C" {
void irq_handler();
void irq_ien();
void irq_ioff();
void bios_flush();
uint32_t v_w0k0();
uint32_t v_w0k1();
uint32_t v_w0k2();
uint32_t v_w0k3();
uint32_t v_w0k4();
uint32_t v_w0k5();
uint32_t v_w0k6();
uint32_t v_w0k7();
uint32_t v_w0k8();
uint32_t v_w0k9();
uint32_t v_w0k10();
uint32_t v_w0k11();
uint32_t v_w0k12();
uint32_t v_w0k13();
uint32_t v_w0k14();
uint32_t v_w0k15();
uint32_t v_w2k0();
uint32_t v_w2k1();
uint32_t v_w2k2();
uint32_t v_w2k3();
uint32_t v_w2k4();
uint32_t v_w2k5();
uint32_t v_w2k6();
uint32_t v_w2k7();
uint32_t v_w2k8();
uint32_t v_w2k9();
uint32_t v_w2k10();
uint32_t v_w2k11();
uint32_t v_w2k12();
uint32_t v_w2k13();
uint32_t v_w2k14();
uint32_t v_w2k15();
extern uint32_t v_w0k0_after[], v_w0k1_after[], v_w0k2_after[], v_w0k3_after[], v_w0k4_after[], v_w0k5_after[], v_w0k6_after[], v_w0k7_after[], v_w0k8_after[], v_w0k9_after[], v_w0k10_after[], v_w0k11_after[], v_w0k12_after[], v_w0k13_after[], v_w0k14_after[], v_w0k15_after[], v_w2k0_after[], v_w2k1_after[], v_w2k2_after[], v_w2k3_after[], v_w2k4_after[], v_w2k5_after[], v_w2k6_after[], v_w2k7_after[], v_w2k8_after[], v_w2k9_after[], v_w2k10_after[], v_w2k11_after[], v_w2k12_after[], v_w2k13_after[], v_w2k14_after[], v_w2k15_after[];
extern volatile uint32_t irq_rec[4];
}

typedef uint32_t (*vfn)();
static const vfn F[] = { v_w0k0, v_w0k1, v_w0k2, v_w0k3, v_w0k4, v_w0k5, v_w0k6, v_w0k7, v_w0k8, v_w0k9, v_w0k10, v_w0k11, v_w0k12, v_w0k13, v_w0k14, v_w0k15, v_w2k0, v_w2k1, v_w2k2, v_w2k3, v_w2k4, v_w2k5, v_w2k6, v_w2k7, v_w2k8, v_w2k9, v_w2k10, v_w2k11, v_w2k12, v_w2k13, v_w2k14, v_w2k15 };
static uint32_t *const A[] = { v_w0k0_after, v_w0k1_after, v_w0k2_after, v_w0k3_after, v_w0k4_after, v_w0k5_after, v_w0k6_after, v_w0k7_after, v_w0k8_after, v_w0k9_after, v_w0k10_after, v_w0k11_after, v_w0k12_after, v_w0k13_after, v_w0k14_after, v_w0k15_after, v_w2k0_after, v_w2k1_after, v_w2k2_after, v_w2k3_after, v_w2k4_after, v_w2k5_after, v_w2k6_after, v_w2k7_after, v_w2k8_after, v_w2k9_after, v_w2k10_after, v_w2k11_after, v_w2k12_after, v_w2k13_after, v_w2k14_after, v_w2k15_after };

static int waitPending() {
    write32(I_MASK, 0);
    write32(I_STAT, ~1u);
    for (uint32_t n = 0; n < LIMIT; n++) if (read32(I_STAT) & 1) return 1;
    return 0;
}

int main() {
    printf("\ncpu/irq-unmask3\n");
    EnterCriticalSection();
    uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    volatile uint32_t *vec = (volatile uint32_t *)0x80000080;
    uint32_t saved[4] = { vec[0], vec[1], vec[2], vec[3] };
    uint32_t h = (uint32_t)irq_handler;
    vec[0] = 0x3C1A0000 | ((h + 0x8000) >> 16);
    vec[1] = 0x275A0000 | (h & 0xFFFF);
    vec[2] = 0x03400008;
    vec[3] = 0;
    bios_flush();
    for (int pass = 0; pass < 3; pass++) {
        for (int w = 0; w < 2; w++) {
            printf("pass %d W%d:", pass, w * 2);
            for (int k = 0; k < 16; k++) {
                int i = w * 16 + k;
                if (!waitPending()) { printf(" --"); continue; }
                irq_rec[0] = irq_rec[1] = irq_rec[2] = irq_rec[3] = 0;
                irq_ien();
                F[i]();
                irq_ioff();
                write32(I_MASK, 0);
                int32_t n = (int32_t)(irq_rec[1] - (uint32_t)A[i]) / 4;
                printf(" k%d:%d%s", k, (int)n, (irq_rec[2] >> 31) ? "b" : "");
            }
            printf("\n");
        }
    }
    vec[0] = saved[0]; vec[1] = saved[1]; vec[2] = saved[2]; vec[3] = saved[3];
    bios_flush();
    write32(I_STAT, ~1u);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
