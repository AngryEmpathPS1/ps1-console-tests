/*
 * spu/spu-readback-small - hwtests/spu-readback with its 8- and 4-word block passes (modes B8, B4) built in (2026-11-13).
 * The same program and the same lines; see ../spu-readback/main.cpp for what each mode is and why. Kept as its own test so the
 * default spu-readback stays the program the three consoles already ran.
 */
#define SMALL_BLOCKS 1
#include "../spu-readback/main.cpp"
