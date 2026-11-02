/*
 * dma/dma-chopx - dma/chopping's own code and ours in one program, a piece at a time (2026-11-01)
 *
 * WHY. hwtests/dma-order and dma-setup have not found why dma/chopping's 2,048 words into 64x64 take 160 to 300 cycles less than ours on one console.
 * This program is chopping's transfer, helpers and all, with its parts changed for ours one at a time, until the number moves.
 *
 * WHAT IT DOES. As dma/chopping: the library's video set-up at 640x480, a screen clear, its random data, the channels masked in DPCR, measure() around
 * the start and waitForChannel. By blocks of 4, 8 and 16 words, twice each, for sixteen variants: bit 1 the rectangle sent by GP0 writes rather than
 * DrawPrim, bit 2 no GP1(01h) and DrawSync before it, bit 4 our poll and Timer 2 reads rather than measure() and waitForChannel, bit 8 a static buffer
 * rather than malloc's. The rectangle moves as chopping's does.
 *
 * READ IT LIKE THIS. L var bs ba x y t, and the line ends (GPU busy) if GPUSTAT bit 26 was clear after. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-chopx --frames=3000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <malloc.h>
#include <dma.hpp>
#include <measure.hpp>
#include <twister.h>
#include <test.h>
#include <io.h>

using namespace DMA;

#define T2_COUNT 0x1F801120u
#define CHCR2    0x1F8010A8u

uint8_t* buffer;
static uint32_t sbuf[2048 + 64] __attribute__((aligned(16)));
const int VRAM_WIDTH = 1024;
const int W = 64;
const int H = 64;
const int TRANSFER_SIZE = W * H * 2;
int position = 0;
CHCR control;

void beginCpuToVramTransfer(int v) {
    if (!(v & 2)) { writeGP1(1, 0); DrawSync(0); }
    uint16_t x = position % (VRAM_WIDTH / W);
    uint16_t y = position / (VRAM_WIDTH / W);
    if (v & 1) {
        write32(0x1F801810u, 0xA0000000u);
        write32(0x1F801810u, ((y * H) << 16) | (x * W));
        write32(0x1F801810u, (H << 16) | W);
    } else {
        CPU2VRAM buf;
        setcode(&buf, 0xA0);
        setlen(&buf, 3);
        setXY0(&buf, x * W, y * H);
        setWH(&buf, W, H);
        DrawPrim(&buf);
    }
    writeGP1(4, 2);
    position++;
    if (position >= 64) position = 0;
}

void generateData(uint8_t* b) {
    for (int i = 0; i < TRANSFER_SIZE; i += 2) {
        uint32_t v = randomMT();
        b[i] = v & 0xff;
        b[i + 1] = (v >> 8) & 0xff;
    }
}

__attribute__((noinline)) static uint32_t timedC(uint32_t chcr) {
    uint32_t p = 0;
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && p < 60000) p++;
    const uint16_t t1 = read16(T2_COUNT);
    return (uint16_t)(t1 - t0);
}

static uint32_t runOne(int v, int bs) {
    uint8_t* b = (v & 8) ? (uint8_t*)sbuf : buffer;
    generateData(b);
    beginCpuToVramTransfer(v);
    auto addr = MADDR((uint32_t)b);
    BCR block = BCR::mode1(bs, TRANSFER_SIZE / bs / 4);
    control = CHCR::VRAMwrite();
    control.syncMode = CHCR::SyncMode::syncBlockToDmaRequests;
    waitForChannel(Channel::GPU);
    write32(CH_BASE_ADDR + 0x10 * (int)Channel::GPU, addr._reg);
    write32(CH_BLOCK_ADDR + 0x10 * (int)Channel::GPU, block._reg);
    uint32_t cycles;
    if (v & 4) cycles = timedC(control._reg);
    else cycles = measure<MeasureMethod::CpuClock>([]() {
        write32(CH_CONTROL_ADDR + 0x10 * (int)Channel::GPU, control._reg);
        waitForChannel(Channel::GPU);
    });
    return cycles;
}

int main() {
    SetVideoMode(MODE_NTSC);
    initVideo(640, 480);
    printf("\ndma/dma-chopx\n");
    printf("L var bs ba t, (GPU busy) if so. No assertions.\n\n");
    clearScreen();
    seedMT(1337);
    EnterCriticalSection();
    masterEnable(Channel::MDECin,  false);
    masterEnable(Channel::MDECout, false);
    masterEnable(Channel::GPU,     true);
    masterEnable(Channel::CDROM,   false);
    masterEnable(Channel::SPU,     false);
    masterEnable(Channel::PIO,     false);
    masterEnable(Channel::OTC,     false);
    buffer = (uint8_t*)malloc(TRANSFER_SIZE);
    printf("buffers %x %x\n", (uint32_t)buffer, (uint32_t)sbuf);
    for (int r = 0; r < 2; r++)
        for (int v = 0; v < 16; v++)
            for (int b = 0; b < 3; b++) {
                const int bs = 4 << b;
                const uint32_t t = runOne(v, bs);
                const bool busy = !(ReadGPUstat() & (1u << 26));
                printf("L %d %d %d %u%s\n", v, bs, TRANSFER_SIZE / bs / 4, t, busy ? " (GPU busy)" : "");
            }
    writeGP1(1, 0);
    DrawSync(0);
    ExitCriticalSection();
    printf("\ndma/dma-chopx: Done.\n");
    for (;;) VSync(0);
}
