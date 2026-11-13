TARGET = pmsplit-replay.elf
# linked at 80100000h: pmsplit-sp's hot code is 80013000h-8001C000h and its data 8001E3B4h-80020208h, 80120568h, 801FFE54h-801FFEBCh
# (experiments/pm386_snap.js)
LDFLAGS = -g -Ttext=0x80100000 -gc-sections -T /usr/local/mipsel-unknown-elf/mipsel-unknown-elf/lib/ldscripts/elf32elmip.x

include ../../common-test.mk

# main.cpp includes the generated snap.h (experiments/pm386_replaygen.js): rebuild when it changes
build/main.o: snap.h
