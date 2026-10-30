# SCPH-5000 (Japan), first day 2026-10-26 - what was plugged in

- Power: 120 V direct (no step-down transformer), a box fan on it. Arrived loose in its original box.
- BIOS: 2.2 12/04/95 J, CRC32 24FC7E17 (dumped twice over Unirom, identical).
- Unirom by FreePSXBoot's 2.2 image lettered "I" (its file hash 24fc7...; the release has no "J" for 2.2), the PSxMemCard
  in slot 1.
- Slot 2: an official Sony memory card that came with the console (Japanese), in from the start of slice 1.
- Port 1 controller: a white Sony DualShock.
- Port 2 controller: a Sony original digital pad, not a DualShock - plugged in during slice 1 at program ~15, before
  ack-per-device (#28).
- No disc (a Japanese drive reads only a Japanese disc; FF7 Japan not here yet).
- Serial: the kit Pico 2 W bridge (COM5 -> tools/serialbridge.ps1 on 127.0.0.1:3335).
- Heat: the power-supply side stayed cool through slice 1 on 120 V with the fan.
