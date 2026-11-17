TARGET = cart-start.elf
# the runner's flags and address, as hwtests/cart-draws: loaded where the runner goes (80180000h), run BEFORE the runner is copied over it
CFLAGS = -g -msoft-float -O2 -G0 -D__PLAYSTATION__ -fno-builtin -fdata-sections -ffunction-sections -Wall
CPPFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -std=c++1z
LDFLAGS = -g -G0 -Ttext=0x80180000 -gc-sections -T /usr/local/mipsel-unknown-elf/mipsel-unknown-elf/lib/ldscripts/elf32elmip.x

include ../../common-test.mk
