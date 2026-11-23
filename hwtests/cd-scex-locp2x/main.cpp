/*
 * cdrom/cd-scex-locp2x - hwtests/cd-scex-locp at DOUBLE speed (Setmode A0h), the Fable CD session, 2026-11-23: whether the sector in
 * flight at the hold's kick is announced a set number of SECTORS after the landing (experiments/land_inflight.js, 1.25) or a set
 * TIME (~16.6 ms at 1x, 2.5 sectors at 2x). Everything else is cd-scex-locp's: read it there.
 */
#define LOCP_MODE 0xA0
#define LOCP_TITLE "\ncdrom/cd-scex-locp2x\n"
#include "../cd-scex-locp/main.cpp"
