/*
 * hwtests/cart-draws - WHAT THIS BOOT DREW, by the runner's own instruments, as a program of its own (cart ROM v4's per-boot draws;
 * owner AngryEmpath, 2026-11-17). Not a test for the runner's protocol: cartrom/modes2.inc run_draws puts it in RAM where the runner
 * goes (80180000h), runs it at the shell break BEFORE the runner is copied in, and the runner starts after as ever - so the runner,
 * its build (PSXRUNNER8, sha1 007f32d3) and everything it snapshots stay as they were.
 *
 * It includes the runner's source whole (its instruments are static there: probeRefresh, probeSpu, the vernier, runner_align and
 * the asm they use), so a draw read here is read by the same code, the same layout, the same cache lines as the runner's own
 * landing (the vernier's reads depend on the line they sit in: "ONE routine's (runner_read16)"). Only the runner's loop is replaced
 * (runner_main, renamed on the way in; _start's `j runner_main` finds this one).
 *
 * WHAT IT DOES, in the runner's own order: interrupts off; Timer 2 at the system clock, four waits of 60,000 as the runner's start;
 * the refresh found (probeRefresh: refreshR0, the first phase of the longest, and the 256-time table); the SPU's sample clock found
 * (probeSpu: spuS0, mod 1,536); then the LOCKED LANDING `run()` makes with no target (any frame): Timer 1 at the system clock, sync
 * mode 1, a VBlank, 20,000 cycles, runner_align on Timer 1 = V (V = 24,000, `--video`'s default) - the second landing after the
 * mode write (the first counts from the write) - the refresh's and the SPU's phase there, and the vernier: phi, the frame's place
 * within a CPU cycle, in 1250ths of a cycle, as vernLo < phi <= vernHi (120 lines read at the tick's offset). The timers' modes and
 * targets, Timer 2, and the SPU registers probeSpu touches go back as found. Everything lands in 1,024 bytes at 80190100h, the cold
 * block's RAM copy + 100h:
 *   word 0 'DRAW', 1 the version (1), 2 refreshR0, 3 spuS0, 4 the refresh phase at the landing ((C - refreshR0) & 255), 5 the SPU
 *   phase ((C - spuS0) mod 1536; FFFFFFFFh: no SPU phase found), 6 runnerFlags (bit 0: the SPU was switched on to find its phase),
 *   7 vernLo, 8 vernHi (phi in 1250ths on the line the board MEASURED, word 14; vernLo >= vernHi: unread), 9 the frames the landing waited, 10 the landing's
 *   Timer 2 (extended), 11 lockPhase (FFFFFFFDh: no VBlank in 2,000,000 cycles; FFFFFFFEh: Timer 1 stood still),
 *   12 Timer 2's count over eight frames (VBlank to VBlank), 13 the line it gives (1250ths of a cycle), 14 the line the vernier used (the nearest of the lines a
 *   console can have, or the measured one), 15 its flags (1 two crystals, 2 one oscillator, 4 unmatched, 8 no measure, 10h 7502 NTSC, 20h PAL),
 *   172 GPUSTAT before the measure, 173 after the landing (the video mode, resolution, interlace; the display RANGE has no register to read - Timer 1's tick follows
 *   its end, so the vernier's offset d in the raw reads is its only trace), 174-178 where runner_align, runner_probe, runner_spuprobe, runner_read16, runner_read32
 *   sit as this program runs (the layout record from the build, words 180-, says where the loops are: tools/cartdraws.js checks them for I-cache collisions),
 *   bytes 64-319: refreshTable[256]; 320-439: the vernier's 120 Timer 1 reads (vernC); 448-687: their offsets d (vernD, int16), so a
 *   phi read "unread" (vernLo >= vernHi) can still be fitted offline (tools/vernfit.js); word 255 is the cart's (the bootprobe log's length).
 * tools/cartsnap.js names them; docs/INSTRUMENT-AUDIT-2.md finding 1 and the discriminator's --phases / --angles read them.
 */
#define runner_main runner_main_orig
#include "../runner/main.cpp"
#undef runner_main

/* THE BOARD'S LINE, MEASURED (busValue's finding, 2026-11-17, approved through main): the vernier's schedule and its board decode need the machine's
 * line, in 1250ths of a cycle, and a cart start has no host to name it: the two crystals' 2,152.5504 and a one-oscillator machine's 2,152.5 differ by 0.0504
 * a line, 6 cycles over the 120 reads - and ~13 cycles a frame. So eight frames are timed with Timer 2 (VBlank edge to VBlank edge, I_STAT bit 0, the
 * extended count): ~4.53 million cycles, the two lines 105 apart against an edge's +-15. The frame's lines come from GPUSTAT bit 20 (263 NTSC, 314 PAL: src/psx.js);
 * the measured line, total * 1250 / (8 * lines), is matched to the lines a console can have - NTSC two crystals 2690688, one oscillator 2690625, the
 * SCPH-7502's NTSC 2715386, PAL two crystals 2709489 - within 28 (a quarter of the nearest gap; the measure's own error is ~9), and that line is the one
 * the vernier is scheduled and decoded on. No match: the measured line itself (flag 4: phi to a cycle or so only), no measure (no VBlank in 2,000,000 cycles):
 * the runner's default, flag 8. lflags: 1 two crystals, 2 one oscillator, 4 unmatched, 8 no measure, 10h 7502-NTSC, 20h PAL. */
/* PAD (cart ROM v4's layout rule, tools/cartstartbuild.js): this function's bytes move measureLine() and runner_main() - the landing's call of runner_align is in the latter -
 * 400h bytes against the runner's assembly (its nop slide), off the slide's I-cache lines. Never called. */
extern "C" __attribute__((used, noinline)) void cart_draws_pad() { __asm__ volatile(".rept 256\n nop\n .endr"); }

struct LineMeas { uint32_t total, meas, used, flags; };
static LineMeas measureLine() {
    LineMeas m = { 0, 0, 2690688u, 0 };
    const uint32_t gst = GPUSTAT, pal = (gst >> 20) & 1, L = pal ? 314u : 263u;
    uint32_t c0 = 0; bool ok = true;
    I_STAT = ~1u;
    for (int e = 0; e <= 8 && ok; e++) {
        const uint32_t w0 = cycles();
        while (!(I_STAT & 1) && cycles() - w0 < 2000000u) {}
        if (!(I_STAT & 1)) { ok = false; break; }
        const uint32_t c = cycles();
        if (e == 0) c0 = c; else if (e == 8) m.total = c - c0;
        I_STAT = ~1u;
    }
    if (!ok) { m.flags = 8; return m; }
    m.meas = (((m.total + 4) >> 3) * 1250u) / L;
    static const uint32_t CAND[4] = { 2690688u, 2690625u, 2715386u, 2709489u };
    static const uint32_t FLAG[4] = { 1, 2, 0x10, 0x20 };
    for (int i = 0; i < 4; i++) {
        const uint32_t d = m.meas > CAND[i] ? m.meas - CAND[i] : CAND[i] - m.meas;
        if (d <= 28 && (i == 3) == (pal != 0)) { m.used = CAND[i]; m.flags = FLAG[i]; return m; }
    }
    m.used = m.meas; m.flags = 4;
    if (pal) m.flags |= 0x20;
    return m;
}

extern "C" void runner_main() {
    volatile uint32_t *out = (volatile uint32_t *)0x80190100u;
    entrySr = getSr();
    setSr(entrySr & ~1u);
    uint16_t m0 = TMODE(0) & 0x3FF, m1 = TMODE(1) & 0x3FF;
    const uint16_t g0 = TTARGET(0), g1 = TTARGET(1);
    const uint16_t xfer = SPU_XFER, cnt = SPUCNT;
    runnerFlags = 0; cyclesHi = 0; lockFrames = 0; lockPhase = 0xFFFFFFFFu; lockSpu = 0xFFFFFFFFu; landT2 = 0; vernLo = vernHi = 0;
    for (int k = 0; k < VERN_N; k++) { vernC[k] = 0xFF; vernD[k] = 0; }
    T2_MODE = 0;
    for (int k = 0; k < 4; k++) { const uint16_t t0 = T2_COUNT; while ((uint16_t)(T2_COUNT - t0) < 60000) {} }
    probeRefresh();
    lastT2 = T2_COUNT;
    probeSpu();
    const uint32_t gst0 = GPUSTAT;
    const LineMeas lm = measureLine();   /* before the plan: the vernier is scheduled and decoded on the measured line */
    vernLine = lm.used;
    const uint32_t delay = 24000;
    vernPlan(delay);
    T1_MODE = 0x0003;                    /* the system clock, sync mode 1: reset to 0 at VBlank */
    uint32_t sinceWrite = 0;
    uint32_t ph = 0xFFFFFFFFu, ps = 0xFFFFFFFFu;
    for (int tries = 0; tries < 8; tries++) {
        I_STAT = ~1u;
        { const uint32_t w0 = cycles(); while (!(I_STAT & 1) && cycles() - w0 < 2000000u) {} }
        if (!(I_STAT & 1)) { lockPhase = 0xFFFFFFFDu; break; }
        const uint16_t te = T2_COUNT;
        while ((uint16_t)(T2_COUNT - te) < 20000) cycles();
        const uint16_t c1 = T1_COUNT;
        { const uint16_t tw = T2_COUNT; while ((uint16_t)(T2_COUNT - tw) < 64) {} }
        if (T1_COUNT == c1) { lockPhase = 0xFFFFFFFEu; break; }
        runner_align(&T1_COUNT, delay);
        const uint32_t C = cycles();
        ph = (C - refreshR0) & 255; ps = spuS0 == 0xFFFFFFFFu ? 0xFFFFFFFFu : mod1536(C - spuS0);
        lockFrames++; sinceWrite++;
        if (sinceWrite > 1) { lockPhase = ph; lockSpu = ps; landT2 = C; vernier(); vernDecode(); break; }
    }
    TTARGET(0) = g0; TMODE(0) = m0; TTARGET(1) = g1; TMODE(1) = m1;
    T2_MODE = 0;
    SPU_XFER = xfer; SPUCNT = cnt;
    I_STAT = ~1u;
    out[0] = 0x57415244u; out[1] = 1; out[2] = refreshR0; out[3] = spuS0; out[4] = ph; out[5] = ps; out[6] = runnerFlags;
    out[12] = lm.total; out[13] = lm.meas; out[14] = lm.used; out[15] = lm.flags;
    out[172] = gst0; out[173] = GPUSTAT; out[174] = (uint32_t)runner_align; out[175] = (uint32_t)runner_probe; out[176] = (uint32_t)runner_spuprobe;
    out[179] = (uint32_t)cart_draws_pad;   /* (kept: the linker would drop it) */
    out[177] = (uint32_t)runner_read16; out[178] = (uint32_t)runner_read32;
    out[7] = (uint32_t)vernLo; out[8] = (uint32_t)vernHi; out[9] = lockFrames; out[10] = landT2; out[11] = lockPhase;
    for (int i = 0; i < 256; i++) ((volatile uint8_t *)out)[64 + i] = refreshTable[i];
    for (int k = 0; k < VERN_N; k++) { ((volatile uint8_t *)out)[320 + k] = vernC[k]; ((volatile int16_t *)((volatile uint8_t *)out + 448))[k] = vernD[k]; }
}
