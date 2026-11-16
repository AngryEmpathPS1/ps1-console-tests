/*
 * cdrom/cd-scex-short - cd-scex for a disc shorter than 40 minutes (2026-11-16). cd-scex parks the head as far out as
 * 00:40:00, which is past the end of Doom (USA), 27:30; this build drops that one row and is otherwise cd-scex itself.
 * WHY: issue AngryEmpathPS1/ps1-console-tests#3 - Rix's stock SCPH-7001 matched no SCEx string anywhere, his chipped
 * SCPH-9002 matched everywhere, ours matched only at 00:00:10 and 00:02:00. Run on its own, with no earlier test in the
 * same power-on having sent the drive's unlock (50h-56h), and on a second pressed disc.
 */
#define SCEX_SHORT_DISC 1
#include "../cd-scex/main.cpp"
