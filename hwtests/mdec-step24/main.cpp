/*
 * mdec/mdec-step24 - ps1-tests' mdec/step-by-step-log, at 24 bits per pixel.
 *
 * Derived from ps1-tests (MIT License, Copyright (c) 2020 Jakub Czekanski):
 * mdec/step-by-step-log/main.cpp, with the colour depth set to 24 bpp and the
 * read-out sized to match. symbols.h is that test's data, unchanged.
 *
 * WHY (ROAD A8, the MDEC dig of 2026-09-27; ROAD B). The upstream test feeds
 * the MDEC one word at a time and prints MDEC_STATUS at every step - input
 * and output - and its console log (15 bpp) is the oracle behind this core's
 * status register: the data-out request (bit 27), the current block field and
 * the Cr lag (M111, M249). It was only ever run at 15 bpp. At 24 bpp an output
 * block is 192 words instead of 128, and the questions the dig left open are
 * exactly the ones a 24-bit log answers: when bit 27 rises and falls against
 * the 26/1 of the 15-bit log, the "17" at 24 bpp, and whether the Cr lag moves
 * in 12-word units.
 *
 * WHAT IT DOES. Exactly the upstream test's steps: four macroblocks of the
 * symbols image, the input fed a word at a time until the input FIFO says
 * full, then the output read a word at a time until it says empty, round and
 * round, MDEC_STATUS printed before every word; then the image hexdumped and
 * drawn. At 24 bpp the four macroblocks are 3,072 bytes (W = 24 halfwords, H =
 * 64, where the 15-bit test had W = 16).
 *
 * READ IT LIKE THIS. Line against line with this core's log; the status words
 * are the result. There are no assertions.
 *
 * ON THIS CORE AT v0.258 (`node hwtests/run.js mdec-step24`), 1,033 status
 * reads over five input/output rounds. Per output round - the words read, and
 * the word at which the current block field steps and bit 27 falls - against
 * the upstream 15-bit CONSOLE log summarised the same way:
 *   24 bpp here    144 (31, 79, req off 119)   144   96 (31, 43, off 71)
 *                  192 (31, 79, 127, 139, off 167)   192 (31, 79, 127, off 191)
 *   15 bpp console  96 (15, 47, off 71)         96   64 (15, 23, off 39)
 *                  128 (15, 47, 79, 87, off 103)     128 (15, 47, 79, off 127)
 * So each 24-bit round is 1.5 times the 15-bit one and the request falls 25
 * words before a round ends at both depths. A console's 24-bit log read
 * against these lines settles bit 27 and the block field at 24 bpp.
 * ANSWERED by the SCPH-5501, 7501 and 9001 (console day, two runs each, all six alike): bit 27 as here; the next
 * quadrant lands (the decoder takes input) at 9 words left, the block field steps at 49 counted and the Cr lag ends
 * at 41 (v0.377.37, src/psx.js MDEC_OUT_NEXT_AT_24) - AT v0.377.37 every one of the 1,033 status words the
 * consoles'. And the low halfword of words 16 and 40 of every quadrant reads the NEXT quadrant's (not modelled;
 * hwtests/mdec-glitch24 asks when).
 */
#include <common.h>
#include <psxsio.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <delay.h>
#include <io.h>
#include <dma.hpp>
#include <mdec.h>
#include <hexdump.h>
#include <gpu.h>
#include "symbols.h"

extern void mdec_data(uint32_t cmd);
int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-step24 (ps1-tests mdec/step-by-step-log at 24 bpp)\n");
    printf("Manually feeds the MDEC data, monitors status register at every step.\n");
    printf("When MDECin FIFO is full, program switches to read mode which dumps decoded image until FIFO out gets empty. \n");

    clearScreen();

    mdec_reset();
    mdec_quantTable(quant, true);
    mdec_idctTable((int16_t*)idct);

    mdec_enableDma();

    const ColorDepth depth = ColorDepth::bit_24;
    const bool outputSigned = false;
    const bool setBit15 = false;

    printf("mdec_decode(addr=0x%08x, length=0x%x, colorDepth=%d, outputSigned=%d, setBit15=%d)... \n", symbols_mdec, symbols_mdec_len, depth, outputSigned, setBit15);
    size_t lengthWords = symbols_mdec_len / 4;
    uint32_t cmd = (1<<29) | (depth<<27) | (outputSigned<<26) | (setBit15 << 25) | (lengthWords & 0xffff);

    while (mdec_cmdBusy());
    mdec_cmd(cmd);

    /* 24 bpp: a 16-pixel row is 48 bytes, 24 halfwords; four 16x16 macroblocks stacked are 64 rows. */
    int W = 24;
    int H = 64;

    const int bytesToRead = W * H * 2;
    int bytesToReadRemaining = bytesToRead;
    uint32_t image[bytesToRead / 4];
    int imagePtr = 0;

    uint16_t* rlePtr = (uint16_t*)symbols_mdec;

    auto printfStatus = []() {
        uint32_t status = read32(MDEC_STATUS);
        printf("==== MDEC_STATUS: 0x%08x, "
            "dataOutFifoEmpty: %d, "
            "dataInFifoFull: %d, "
            "cmdBusy: %d, "
            "dataInReq: %d, "
            "dataOutReq: %d, "
            "currentBlock: %d\n",
            status,
            (status & (1 << 31)) != 0,
            (status & (1 << 30)) != 0,
            (status & (1 << 29)) != 0,
            (status & (1 << 28)) != 0,
            (status & (1 << 27)) != 0,
            (status >> 16) & 7
        );
    };

    while (true) {
        bool waitForImage = false;
        printf("\n\n----- MDECin\n");
        for (;lengthWords != 0;lengthWords--) {
            printfStatus();
            if (mdec_dataInFifoFull()) {
                printf("MDEC: dataInFifoFull, suspending 'mdecIn'\n");
                break;
            }
            uint32_t word = *rlePtr++;
            word |= ((uint32_t)(*rlePtr++)) << 16;

            mdec_data(word);
            printf("MDEC_DATA <- 0x%08x\n", word);
            delay(10000);

            waitForImage = true;
        }
        if (waitForImage) {
            while (mdec_dataOutFifoEmpty());
        }

        printf("\n\n----- MDECout\n");
        for (; bytesToReadRemaining != 0; bytesToReadRemaining -= 4) {
            printfStatus();
            if (mdec_dataOutFifoEmpty()) {
                printf("MDEC: dataOutFifoEmpty, suspending 'mdecOut'\n");
                break;
            }
            uint32_t read = mdec_read();
            printf("MDEC_DATA -> 0x%08x\n", read);
            image[imagePtr++] = read;
            delay(10000);
        }

        if (lengthWords <= 0 && bytesToReadRemaining <= 0) {
            break;
        }
    }

    printfStatus();
    printf("done\n");

    hexdump((uint8_t*)image, bytesToRead, 32);
    writeGP0(1, 0);
    vramWriteDMA(0, 0, W, H, (uint16_t*)image);

    write32(DMA::CH_CONTROL_ADDR + 0x10 * (int)DMA::Channel::MDECin, 0);
    write32(DMA::CH_CONTROL_ADDR + 0x10 * (int)DMA::Channel::MDECout, 0);
    write32(DMA::CH_CONTROL_ADDR + 0x10 * (int)DMA::Channel::GPU, 0);
    DMA::masterEnable(DMA::Channel::MDECout, false);
    DMA::masterEnable(DMA::Channel::MDECin, false);
    DMA::masterEnable(DMA::Channel::GPU, false);

    mdec_reset();
    printf("Done\n");
    for (;;) {
        VSync(0);
    }
    return 0;
}
