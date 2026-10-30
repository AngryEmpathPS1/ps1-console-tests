# PS1 console tests

Timing and behaviour tests for the original PlayStation, and what they read on real consoles.

- **`consoles/`**: the logs, one folder per console, then one per session. Each console has a page saying what it reported about itself.
- **`hwtests/`**: the test programs, each a folder with its source, beside a ready-to-run PS-EXE in `hwtests/build/`.
- **`findings/`**: what the logs show, by subsystem, each claim with the tests and consoles behind it.
- **`TESTS.md`**: what each test measures.
- **`COVERAGE.md`**: every test against every console, with run counts.
- **`comparisons/`**: one console's logs against another's, line by line.

## Consoles

SCPH-1001, 5000, 5501 (two units), 7001, 7501, 7502 and 9001 (and a borrowed second 9001). The disc for the CD tests was The X-Files (USA) Disc 1 unless a test says otherwise. Discs and BIOS images are not here.

## Run a test

Send a PS-EXE from `hwtests/build/` to a console with any loader, such as [Unirom](https://github.com/JonathanDotCel/unirom8_bootdisc_and_firmware_for_ps1) and nops, and read its TTY output. Most finish in under a minute. Tests print measurements, not pass or fail: compare with the logs. Each test's header says what it does and how to read it. `PREDICTED.txt`, where a folder has one, is what was expected before the test first ran on a console.

Most logs were taken through `hwtests/runner`, a small resident program that gives every test the same locked start (video, DRAM refresh, SPU), so that phase-sensitive rows land in the same place on every run. Without it those rows land elsewhere. Its PC side is `tools/hwrunner.js`, which talks to the console through a serial link on a TCP socket (`tools/serialbridge.ps1` makes one on Windows):

```
node tools/hwrunner.js store-cost --link=127.0.0.1:3335 --lock --out=mylogs
```

**A few tests can freeze a console, which needs a RESET and does no harm.** `biu-sched` and `biu-dram` flip bus configuration bits, one at a time, from uncached code, and restore them. Do not run `biu-dram` around CD loading on EARLY-PU-8 or LATE-PU-8 boards: psx-spx reports that clearing that bit hangs CD loading on those. No CD test seeks past 74:00.

## Build a test

The tests build with the Docker toolchain of [ps1-tests](https://github.com/JaCzekanski/ps1-tests) and need its `common` library: see `hwtests/README.md`.

## Not included

- The emulator these tests were written for. It stays private. Test comments mention it here and there, with file names, version numbers and milestone numbers like M207. Ignore them: they are a trail nobody outside can follow.
- That emulator's grades against these logs.
- Six PocketStation tests (`pocket-load`, `pocket-pa3`, `pocket-probe`, `pocket-verify`, `pocket-restore`, `pocket-dump`) and their logs: they carry game data or a personal card backup.
- BIOS images, disc images and drive firmware.

## How this was made

The tests, the analysis and these pages were written by an AI (Claude, by Anthropic), working with one person, who owns the consoles and ran them. The logs are what the consoles printed over serial, untouched.

Don't take the findings on trust. Each one names the tests and consoles behind it, and the logs are the evidence: if a finding and a log disagree, the log wins, and please say so.

## Standing on

None of this happens without other people's work: [psx-spx](https://psx-spx.consoledev.net/) and nocash's no$psx documentation, [ps1-tests](https://github.com/JaCzekanski/ps1-tests) by Jakub Czekański, [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) by Lameguy64, [Unirom](https://github.com/JonathanDotCel/unirom8_bootdisc_and_firmware_for_ps1) by JonathanDotCel, PeterLemon's and amidog's test programs, and the source of DuckStation, PCSX-Redux, mednafen and the MiSTer PSX core. Where a finding is someone else's measurement, it says whose.

## Licences

- **Tests** (`hwtests/`): MIT, in `LICENSE`.
- **Logs and documents** (`consoles/`, `comparisons/`, `findings/`, `TESTS.md`, `COVERAGE.md`): [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), in `LICENSE-DATA`. Use them for anything; credit **AngryEmpathPS1**.
- The test programs link [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) (MPL 2.0, unmodified) and the `common` library of ps1-tests (MIT). Their notices are in `NOTICE`.

Corrections are welcome: open an issue and say how you know.
