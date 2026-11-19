/*
 * cdrom/cd-scex-edge1w - cd-scex-edge1 out to 00:02:69 (2026-11-18). cd-scex-edge1 on the SCPH-1001 matched at every
 * frame of 00:02:40-00:02:55 (six runs), where cd-scex-edge's 00:02:60 matched nothing, so its edge lies past the range
 * Rix asked for. On the 1001 and 7001 Test04 reads forward from the parked position for the whole wait (sectors to about
 * 00:04:4x); on the 9001 the drive stays paused (stat 02, no sectors), so the two kinds of drive sample different stretches.
 * SAFE FOR THE DRIVE: nothing past 00:02:69, about 100 s.
 */
#define EDGE1_WIDE 1
#define EDGE1_LAST 69
#include "../cd-scex-edge1/main.cpp"
