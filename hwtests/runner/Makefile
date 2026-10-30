TARGET = runner.elf
# -G0: no GP-relative data - runner_putchar runs on the test's GP. The runner lives at 80180000h: above every test, below gpu/bandwidth's fixed buffer at 801A0000h (main.cpp says why).
CFLAGS = -g -msoft-float -O2 -G0 -D__PLAYSTATION__ -fno-builtin -fdata-sections -ffunction-sections -Wall
CPPFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -std=c++1z
LDFLAGS = -g -G0 -Ttext=0x80180000 -gc-sections -T /usr/local/mipsel-unknown-elf/mipsel-unknown-elf/lib/ldscripts/elf32elmip.x

include ../../common-test.mk
