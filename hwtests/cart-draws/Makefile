TARGET = cart-draws.elf
# the runner's own flags and address (80180000h): the cart (cartrom/modes2.inc run_draws) loads this where the runner goes and runs it
# BEFORE the runner is copied over it, so the runner's build stays as it was
CFLAGS = -g -msoft-float -O2 -G0 -D__PLAYSTATION__ -fno-builtin -fdata-sections -ffunction-sections -Wall
CPPFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -std=c++1z
LDFLAGS = -g -G0 -Ttext=0x80180000 -gc-sections -T /usr/local/mipsel-unknown-elf/mipsel-unknown-elf/lib/ldscripts/elf32elmip.x

include ../../common-test.mk
