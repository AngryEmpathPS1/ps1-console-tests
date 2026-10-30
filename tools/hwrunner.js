'use strict';
/*
 * tools/hwrunner.js - run tests under hwtests/runner on a console, over a serial link on a TCP socket.
 *
 *   node tools/hwrunner.js <test>... --link=127.0.0.1:3335
 *     --delay=N      cycles after the VBlank edge before each test starts (default 1000)
 *     --lock[=P]     the LOCKED START: each test to the cycle on the video (Timer 1, reset at VBlank, at --video=V,
 *                    default 24000) and on the DRAM refresh (phase P of its 256-cycle grid, default 0, within --tol=N,
 *                    default 0). hwtests/runner/main.cpp, "THE LOCKED START". The summary line says how many frames it
 *                    waited and the phase it started at; --refresh-out=<file> keeps the runner's refresh table (one RAM
 *                    load timed at each of the grid's 256 phases).
 *     --spu          (with --lock=P) the start locked on the SPU's sample clock instead: P is the phase of two of its
 *                    768-cycle samples (0-1535), which fixes the refresh's too; about 26 s of frames on average.
 *     --out=<dir>    write each log as <test>-runner.txt there (default: print)
 *     --timeout=S    seconds a test may take before the run gives up on it (default 600)
 *     --raw          print each log alone, nothing of the runner's
 *     --repeat=N     each test N times in a row (with --lock: each at another frame, so at another phase of the video
 *                    within a CPU cycle, the one thing a locked start leaves free)
 *     --json         print one JSON array of every run
 *
 * The runner is sent once through Unirom's menu (SEXE, no debug mode); from then on this talks its protocol. Each test:
 * its EXE loaded ("OKAY"), run ("DONE", the log's length, the cycles it took), its log fetched and checked. A <test> is a
 * hwtests name or a path to a PS-X EXE. The log holds exactly what the test printed, from its title line.
 *
 * On Windows, tools/serialbridge.ps1 puts a serial port on that socket. Anything else that passes bytes both ways
 * unchanged will do. Some options the code still reads are for an emulator that is not part of this repository.
 */
const fs = require('fs');
const path = require('path');
const net = require('net');
const { Unirom } = require('./unirom.js');

const REPO = path.resolve(__dirname, '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const a = argv.find((x) => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const flag = (k) => argv.includes('--' + k);

class RunnerLink {
  constructor(link) {
    this.link = link; this.q = []; this.waiter = null;
    this.off = link.onData((b) => { for (const x of b) this.q.push(x); if (this.waiter) { const w = this.waiter; this.waiter = null; w(); } });
  }
  write(...parts) { for (const p of parts) this.link.write(p instanceof Uint8Array ? p : Uint8Array.from(p)); }
  /* idleMs: while bytes keep coming, the deadline moves on - a slow line is not a dead one. The SCPH-7501 (2026-10-08
   * evening, a 25-process gate suite on the host starving the serial bridge): a log came back at 10-40 bytes a second, the
   * fixed deadline called the console deaf mid-log, and the recovery talked Unirom's protocol to a runner still sending -
   * after which that console's SPU wrote no capture buffer until a reset (a stray byte read as a command is the likely
   * way). */
  async read(n, ms = 10000, idleMs = 0) {
    let end = this._now() + ms, have = this.q.length;
    while (this.q.length < n) {
      if (idleMs && this.q.length > have) { have = this.q.length; end = Math.max(end, this._now() + idleMs); }
      const left = end - this._now();
      if (left <= 0) throw new Error(`runner: ${this.q.length} of ${n} bytes in ${ms} ms`);
      // the virtual console: in its own time (VirtualConsole.wait), the same every run
      if (this.link.wait) { const n0 = this.q.length; this.link.wait(Math.min(left, 250), () => this.q.length > n0); continue; }
      await new Promise((res) => { const t = setTimeout(() => { this.waiter = null; res(); }, Math.min(left, 250)); this.waiter = () => { clearTimeout(t); res(); }; });
    }
    return Uint8Array.from(this.q.splice(0, n));
  }
  async expect(text, ms) {
    const got = Buffer.from(await this.read(text.length, ms)).toString('latin1');
    if (got !== text) throw new Error(`runner: wanted "${text}", got ${JSON.stringify(got)}`);
  }
  async word(ms) { const b = await this.read(4, ms); return (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)) >>> 0; }
  static w(v) { return [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255]; }
  /* Until the banner: what came before it (Unirom's chatter) is dropped. */
  async banner(ms = 15000) {
    const end = this._now() + ms;
    let win = '';
    while (this._now() < end) {
      const b = await this.read(1, end - this._now());
      win = (win + String.fromCharCode(b[0])).slice(-11);
      if (/^PSXRUNNER[1-9]\n$/.test(win)) { this.version = +win[9]; return; }
    }
    throw new Error('runner: no banner');
  }
  async ping() { this.write([0x50]); await this.expect('PONG', 5000); }
  /* A pause: on the virtual console in its own time (VirtualConsole.idle - the same every run), on a console the host's. */
  idle(ms) { return this.link.idle ? this.link.idle(ms) : new Promise((res) => setTimeout(res, ms)); }
  /* The deadlines' clock: the virtual console's own (link.now), else the host's (tools/unirom.js _now says why). */
  _now() { return this.link.now ? this.link.now() : Date.now(); }
  /* (v5) 'V': the resident runner's identity line, or null - nothing, or Unirom's echo. The line is let go quiet first
   * (300 ms with nothing, at most 5 s) and the answer looked for anywhere in what comes back, twice: after a run cut off
   * mid-exchange the rest of its log was still arriving, the answer came behind it, and a check for a line that STARTS
   * with it waited for a RESET with the runner alive (a forced timeout on the virtual console, 2026-10-08). */
  async ident() {
    const quiet = async () => {
      const end = this._now() + 5000;
      let n = -1;
      while (this._now() < end && n !== this.q.length) { n = this.q.length; await this.idle(300); }
      this.q.length = 0;
    };
    for (let tries = 0; tries < 2; tries++) {
      await quiet();
      this.write([0x56]);
      let text = '';
      const end = this._now() + 1500;
      while (this._now() < end) {
        try { text += String.fromCharCode((await this.read(1, end - this._now()))[0]); } catch (e) { break; }
        const m = /PSXRUNNER[5-9] [^\n]*\n/.exec(text);
        if (m) { await quiet(); return m[0]; }
        if (text.length > 4096) break;
      }
    }
    await quiet();
    return null;
  }
  async load(exe) {
    let sum = 0; for (const b of exe) sum = (sum + b) >>> 0;
    this.write([0x4C], RunnerLink.w(exe.length), exe, RunnerLink.w(sum));
    const got = Buffer.from(await this.read(4, 30000 + exe.length)).toString('latin1');
    if (got === 'BIG!') throw new Error("runner: the program reaches 80130000h, where the runner keeps its copy of the kernel, its log and itself");
    if (got !== 'OKAY') throw new Error(`runner: load answered ${JSON.stringify(got)}`);
  }
  /* end: { text, count } - the count-th line starting with text ends the run (default: its "Done." line);
   * stoppable: the PC may stop it with 'X' (stop()). */
  async run(delay, ms, { end = null, stoppable = true, lock = null } = {}) {
    const mode = (end ? 1 : 0) | (stoppable ? 0x80 : 0);
    const spec = [mode];
    if (end) { const t = Buffer.from(end.text, 'latin1'); spec.push(t.length, ...t, end.count || 1); }
    if (lock) {
      if (this.version < 2) throw new Error('runner: --lock needs runner v2 (PSXRUNNER2)');
      if (lock.spu && this.version < 3) throw new Error('runner: --spu needs runner v3 (PSXRUNNER3)');
      /* (v4) the frame's place within a CPU cycle asked for, as the vernier reads it, in 625ths of a cycle - none from a
       * console; the virtual console asks for the console's (hwtests/runner/main.cpp "THE VIDEO WITHIN A CYCLE") */
      // (v6) phi in 1250ths of a cycle, and two more words: the line (1250ths; 0 = the runner's two-crystal default) and
      // a GP1(06h) range written before the landing (bit 31 = apply)
      const U = this.version >= 6 ? 1250 : 625;
      const phiT = this.version >= 4 && lock.phi !== undefined && lock.phi !== null ? Math.round(lock.phi * U) : 0x7FFFFFFF;
      const phiTol = Math.round((lock.phiTol === undefined ? (lock.spu ? 0.16 : 0.04) : lock.phiTol) * U);
      // (v7) the range word's bit 30: GPUSTAT bit 31's toggle read too; bits 24-26: a display width (1-5: 256, 320, 512,
      // 640, 368) set before the landing
      if ((lock.toggle || lock.width) && this.version < 7) throw new Error('runner: --toggle and --width need runner v7 (PSXRUNNER7)');
      const WIDTH = { 256: 1, 320: 2, 512: 3, 640: 4, 368: 5 };
      if (lock.width && !WIDTH[lock.width]) throw new Error('runner: --width is one of 256, 320, 368, 512, 640');
      const rangeWord = (lock.hrange ? (0x80000000 | (lock.hrange.x2 << 12) | lock.hrange.x1) : 0) |
        (lock.toggle ? 0x40000000 : 0) | ((lock.width ? WIDTH[lock.width] : 0) << 24);
      const v6 = this.version >= 6 ? [...RunnerLink.w(lock.lineLen ? Math.round(lock.lineLen * 1250) >>> 0 : 0),
        ...RunnerLink.w(rangeWord >>> 0)] : [];
      this.write([lock.spu ? 0x55 : 0x53], RunnerLink.w(lock.video), RunnerLink.w(lock.phase), [lock.tol],
        this.version >= 4 ? [...RunnerLink.w(phiT >>> 0), ...RunnerLink.w(phiTol >>> 0)] : [], v6, spec);
      const d = await this.finish(ms);
      d.frames = await this.word(); d.phase = await this.word();
      if (this.version >= 3) {
        const sp = await this.word(), s0 = await this.word(), r0 = await this.word();
        d.spuPhase = sp === 0xFFFFFFFF ? null : sp;
        d.spuS0 = s0 === 0xFFFFFFFF ? null : s0;
        d.r0 = r0;
        // how the refresh sits against the SPU on this console (hwtests/runner/main.cpp "THE SPU's SAMPLE CLOCK")
        d.spuRefresh = d.spuS0 === null ? null : (d.spuS0 - r0 + 256 * 3) % 256;
      }
      if (this.version >= 4) {
        d.flags = await this.word();
        d.spuSwitchedOn = !!(d.flags & 1);   // the SPU was off and the probe switched it on to find its phase
        const lo = (await this.word()) | 0, hi = (await this.word()) | 0;
        // the vernier's reads, kept: Timer 1's count and the offset d each was read at (a console whose ticks sit
        // elsewhere than this core's reads "phi unread", and these say where - the SCPH-7501, 2026-10-08)
        // (v5: d is 16 bits - the search reaches -512..+512)
        const n = (await this.read(1))[0], w = this.version >= 5 ? 3 : 2, raw = await this.read(w * n);
        d.vern = [];
        for (let k = 0; k < n; k++) d.vern.push([raw[w * k], w === 3 ? ((raw[w * k + 1] | (raw[w * k + 2] << 8)) << 16) >> 16 : (raw[w * k + 1] << 24) >> 24]);
        const U = this.version >= 6 ? 1250 : 625;
        d.phiLo = lo / U; d.phiHi = hi / U;
        d.phi = lo < hi && lo > -0x7FFFFFFF && hi < 0x7FFFFFFF ? Math.round((lo + hi) / 2) / U : null;
        // (v7) GPUSTAT bit 31 at each of the toggle's reads, and the offset it was read at (tools/vernfit.js --toggle)
        if (lock.toggle) {
          const m = (await this.read(1))[0], tr = await this.read(3 * m);
          d.tog = [];
          for (let k = 0; k < m; k++) d.tog.push([tr[3 * k], ((tr[3 * k + 1] | (tr[3 * k + 2] << 8)) << 16) >> 16]);
        }
      }
      return d;
    }
    this.write([0x52], RunnerLink.w(delay), spec);
    return this.finish(ms);
  }
  /* 'F' (runner v2): the refresh table - the grid's first phase and one RAM load's time at each of its 256 phases. */
  async refresh() {
    this.write([0x46]);
    const r0 = (await this.read(1))[0], t = await this.read(256), sum = await this.word();
    let s = 0; for (const b of t) s += b;
    if ((s >>> 0) !== sum) throw new Error("runner: the refresh table's sum is wrong");
    if (this.version < 3) return { r0, table: Array.from(t) };
    const s0 = await this.word();
    return { r0, table: Array.from(t), spuS0: s0 === 0xFFFFFFFF ? null : s0, spuRefresh: s0 === 0xFFFFFFFF ? null : (s0 - r0 + 768) % 256 };
  }
  /* The answer to 'R': "DONE", the characters printed, the cycles, how it ended. */
  async finish(ms) {
    await this.expect('DONE', ms);
    const len = await this.word(), cycles = await this.word(), how = (await this.read(1))[0];
    return { len, cycles, how };
  }
  stop() { this.write([0x58]); }
  async log() {
    this.write([0x47]);
    const len = await this.word();
    /* 240 s of silence once it has begun (was 15): a borrowed SCPH-9001 (2026-10-12) stopped twice at 398 bytes of the log
     * and sent the rest minutes later, by itself - put() waits on SR.0 with no timeout, so nothing is lost while it
     * waits. A console that sends nothing at all still fails at 30 s + len. */
    const bytes = await this.read(len, 30000 + len, 240000);
    const sum = await this.word();
    let s = 0; for (const b of bytes) s = (s + b) >>> 0;
    if (s !== sum) throw new Error('runner: the log\'s sum is wrong');
    return Buffer.from(bytes).toString('latin1');
  }
}

/* A link to a console (serialbridge's socket) with the same shape as the virtual console's. */
function socketLink(spec) {
  const [host, port] = spec.split(':');
  const sock = net.connect(+port, host);
  const listeners = new Set();
  sock.on('data', (b) => { for (const fn of listeners) fn(b); });
  return new Promise((res, rej) => {
    sock.on('connect', () => res({ write: (b) => sock.write(Buffer.from(b)), onData: (fn) => { listeners.add(fn); return () => listeners.delete(fn); }, close: () => sock.destroy() }));
    sock.on('error', rej);
  });
}

function discFor() { return null; }

function exeOf(name) {
  if (/\.exe$/i.test(name) && fs.existsSync(name)) return path.resolve(name);
  const p = path.join(process.env.PSX_HWEXE ? path.resolve(process.env.PSX_HWEXE) : path.join(REPO, 'hwtests', 'build'), name + '.exe');   // consolegrade's catalogue
  if (!fs.existsSync(p)) throw new Error('no such program: ' + name);
  return p;
}

/* The runner up on a link: sent through Unirom's menu, its banner read. */
/* The runner's file: PSX_RUNNER_EXE names another build (v6 was tried as runner6.exe while the SCPH-5501 ran unattended
 * on v5 - a new runner.exe would have sent its next run to Unirom, which was gone). */
const RUNNER_EXE = process.env.PSX_RUNNER_EXE ? path.resolve(process.env.PSX_RUNNER_EXE) : path.join(REPO, 'hwtests', 'build', 'runner.exe');
async function startRunner(link, beforeExe = null) {
  const r = new RunnerLink(link);
  /* (v5) A runner still resident from the last run, of the build that would be sent, is used as it is: its 'V' line is in
   * runner.exe. Until v5 every run went through Unirom's menu, and a console the last run had left in the runner needed
   * a RESET and the memory card first (the SCPH-7501, 2026-10-08). Unirom's menu only echoes a stray 'V'. */
  const id = await r.ident();
  if (id && fs.readFileSync(RUNNER_EXE).includes(Buffer.from(id, 'latin1'))) {
    r.version = +id[9]; r.reused = true; await r.ping(); return r;
  }
  const u = new Unirom(link);
  await u.hello();
  if (beforeExe) { try { await beforeExe(u); } catch (e) { console.log('  The freeze dump failed: ' + e.message); } }   // the freeze dump (consolerun): a look, not the run
  await u.runExe(fs.readFileSync(RUNNER_EXE));
  await r.banner();
  await r.ping();
  return r;
}

/* The line a program ends on, where it is not "Done." (tools/consolerun.js END: cdrom/timing's source ends in a loop
 * after its fifth double-speed row, getloc prints "Test passed" or "Test failed"), and whether it uses SIO1 itself. */
const END_LINE = { timing: { text: 'double-speed timing:', count: 5 }, getloc: { text: 'Test ', count: 1 } };
/*
 * THE TESTS STARTED ON THE SPU's CLOCK ('U', runner v3; tools/consolerun.js and tools/consolegrade.js both read this):
 * every hwtests program whose source touches the SPU's registers for its sound or its transfers, and ps1-tests'
 * spu/memory-transfer. Each costs ~26 s of waiting a run on a console, against ~4 s for the refresh's lock. Left out:
 * bus-latch and mem-delay (they read SPU registers as bus targets and read one log under 'S'), and the CD tests that
 * reach the SPU (cd-xa, cd-adpbusy, cd-da-capture), whose drive runs on its own clock either way.
 */
const SPU_LOCKED = new Set(['spu-busy', 'spu-curvol', 'spu-decode-ahead', 'spu-dma', 'spu-irq', 'spu-latch', 'spu-lead', 'spu-lead-phase', 'spu-regs',
  'spu-reverb', 'spu-reverb-in', 'spu-xfer', 'dma-rates', 'concurrent-dma', 'dma-arb', 'spu/memory-transfer']);
const USES_SIO1 = /^sio1-/;
const HOW = { 1: 'ended at its end line', 2: 'returned from main', 3: 'stopped from the PC' };

async function runTests(r, names, { delay = 1000, timeout = 600, out = null, lock = null, accept = null } = {}) {
  const results = [];
  for (const name of names) {
    const exe = fs.readFileSync(exeOf(name));
    const base = path.basename(name).replace(/\.exe$/i, '');
    let done, t0;
    for (let tries = 0; ; tries++) {
      await r.load(exe);
      t0 = Date.now();
      done = await r.run(delay, timeout * 1000, { end: END_LINE[base] || null, stoppable: !USES_SIO1.test(base), lock });
      if (!accept || accept(exe, done) || tries >= 7) break;
      await r.log();
    }
    let text = await r.log();
    if (done.len > text.length) text += `\n[runner: the log keeps the first ${text.length} of ${done.len} characters]\n`;
    results.push({ name, ...done, text, secs: (Date.now() - t0) / 1000 });
    if (out) {
      fs.mkdirSync(out, { recursive: true });
      fs.writeFileSync(path.join(out, base + '-runner.txt'), text);
    }
  }
  return results;
}

/*
 * THE DISC GOES IN AS ON A CONSOLE DAY (shared 2026-10-20 by the runner's references here and tools/consolerun.js's
 * rehearsal, which had put it in in one instant): the lid, the disc, 600 frames to settle, and on the drive's own
 * program a second with the lid open first and a Getstat after (each block below says why).
 */
async function discIn(vc, disc) {
  vc.psx.openLid();
  /* ...WITH THE LID OPEN A SECOND FIRST on the drive's own program (2026-10-09): it learns of a disc from the door
   * switch, and a lid opened and shut in one instant left SC430940 never seeing it - stat 00 for 10 s, every command
   * after refused (hwtests/cd-gameday). The model takes the instant as before, so its references do not move. */
  if (vc.psx.cdFirmware) vc.runFrames(60);
  vc.psx.insertDisc(disc);                  // its own licence string (Disc.regionOf), as cputest.js loads it
  vc.runFrames(600);   // counted, not timed by the host (VirtualConsole.runFrames says why)
  /* ...AND A GETSTAT after it, on the drive's own program (2026-10-20). The lid's cycle sets the program's stat bit
   * 4 (shell open), and only Getstat resets it (psx-spx "Getstat - Command 01h": "does reset the shell open flag ...
   * different as for most or all other commands"). Every console's cd-init answers its FIRST Init 02h after the
   * console day's lid cycle (REAL CONSOLE/5 CONSOLE DAY: Unirom up, then the disc in and the lid shut) - something
   * resident there asked Getstat - where SC430940 here answered 12h on every Init of every runner reference
   * (tools/cdscore.js: cd-init 85.7 -> 61.9% within on the 7501, half of it this bit). So the runner asks it once,
   * acknowledges it as a driver would, and leaves no interrupt pending. The model's references do not move. */
  if (vc.psx.cdFirmware) {
    const cd = vc.psx.cdrom;
    cd.write8(0, 0); cd.write8(1, 0x01);
    vc.runFrames(3);
    cd.write8(0, 1); cd.write8(3, 0x1F); cd.write8(0, 0);
    vc.psx.iStat &= ~(1 << 2);   // IRQ_CDROM
    vc.psx.updateIrqLine();
  }
}

module.exports = { RunnerLink, startRunner, runTests, socketLink, discFor, discIn, exeOf, END_LINE, USES_SIO1, HOW, SPU_LOCKED };

if (require.main === module) {
  (async () => {
    const names = argv.filter((a) => !a.startsWith('--'));
    if (!names.length) { console.error('usage: node tools/hwrunner.js <test>... --link=host:port [--delay=N] [--out=dir]'); process.exit(2); }
    let link, vc = null;
    if (opt('link')) link = await socketLink(opt('link'));
    else { console.error('--link=host:port'); process.exit(2); }
    const r = await startRunner(link);
    /*
     * THE DISC GOES IN AFTER THE RUNNER IS UP, as on a console (REAL CONSOLE/5 CONSOLE DAY/README.txt: Unirom started from
     * the memory card in the BIOS shell, then the disc in and the lid shut). Given one at power-on the virtual console's
     * BIOS boots it, and its disc path leaves COM_DELAY 0000132Ch and the CD's delay/size 20943h where the shell leaves
     * 00031125h and 20843h - every BIOS ROM fetch (printf runs from ROM) and every CD register access slower: load-pairs,
     * which never touches the drive, read 54 of its 77 lines differently under the locked start (2026-10-08). Then 600
     * frames for the drive to spin up and settle, as a console's disc sits in for minutes before its first test.
     */
    if (vc && vc.discLater) await discIn(vc, vc.discLater);
    const lockArg = argv.find((a) => a === '--lock' || a.startsWith('--lock='));
    const lock = lockArg ? { video: +opt('video', 24000), phase: lockArg === '--lock' ? 0 : +lockArg.slice(7), tol: +opt('tol', 0), spu: flag('spu'),
      ...(opt('phi') !== undefined ? { phi: +opt('phi') } : {}), ...(opt('phi-tol') !== undefined ? { phiTol: +opt('phi-tol') } : {}),
      /* (runner v6) the vernier's line: --line=<cycles>, else on the virtual console its own (3,412.5 video ticks at its
       * ratio - 2,152.5 on a one-oscillator model, VIDEO_RATIO_CY2081); and --hrange=<x1>,<x2> (hex) for GP1(06h) */
      ...(opt('line') !== undefined ? { lineLen: +opt('line') } : vc ? { lineLen: 3412.5 * vc.psx.gpu.vDen / vc.psx.gpu.vNum } : {}),
      ...(opt('hrange') !== undefined ? { hrange: { x1: parseInt(opt('hrange').split(',')[0], 16), x2: parseInt(opt('hrange').split(',')[1], 16) } } : {}),
      /* (runner v7) --toggle: GPUSTAT bit 31's toggle read beside the tick; --width=<256|320|368|512|640> before the landing */
      ...(flag('toggle') ? { toggle: true } : {}), ...(opt('width') !== undefined ? { width: +opt('width') } : {}) } : null;
    if (vc && opt('spu-refresh') !== undefined) {
      const want = ((+opt('spu-refresh') % 256) + 256) % 256;
      const f0 = await r.refresh();
      if (f0.spuRefresh === undefined || f0.spuRefresh === null) throw new Error('--spu-refresh: the runner found no SPU phase (runner v3 reads SPUSTAT bit 11)');
      // spuRefresh = (spuS0 - refreshR0) mod 256: a refresh x cycles later takes x off it
      vc.psx.refreshDue += (f0.spuRefresh - want + 256) % 256;
    }
    let match = null;
    if (vc && opt('match') !== undefined) {
      const [mr, ms, mp] = opt('match').split(',');
      match = { r: +mr, s: ms === 'null' || ms === '' ? null : +ms, phi: +mp };
      const tol = +opt('phi-tol', 0.04);   // the grids moved, the console's own fraction is one of those that come
      /* the probe: a program landed on phi alone (its log is not kept). Not the test itself: timer-sync and pal-dots
       * switch the video to PAL and back, and frames of another length put every landing after them on another coset
       * of the (refresh, SPU, phi) lattice - the shifts made from the probe's landing then never come round (timer-sync's
       * matched lock ran to its 32,768-frame cap and landed at refresh 155, SPU 263 for 0, 620), and machine-id's own
       * GP1(10h) left GPUREAD 2 for the run that followed it. load-pairs touches neither the video, the drive nor the
       * SPU: on the same coset counter-bracket's came round in 4,896 frames, under the 5,000 the lattice allows (625
       * frames bring phi back, and each 625 move the SPU 192 cycles of its 1,536 and the refresh 64 of its 256).
       * AND BEFORE EVERY RUN, not once: a test that leaves PAL frames behind moves the coset for the run after it too -
       * pal-dots' first matched run landed in 4,870 frames and its second ran to the cap (refresh 19, SPU 127). */
      const probe = opt('probe', 'load-pairs');
      match.land = async () => {
        await r.load(fs.readFileSync(exeOf(probe)));
        const p = await r.run(+opt('delay', 1000), 900000, { end: END_LINE[path.basename(probe).replace(/\.exe$/i, '')] || null,
          stoppable: true, lock: { video: +opt('video', 24000), phase: 9999, tol: 0, spu: false, phi: match.phi, phiTol: tol } });
        await r.log();
        const psx = vc.psx;
        // a refresh x cycles later takes x off the refresh phase a landing reads; an SPU grid x later takes x off its phase
        const dr = (((p.phase - match.r) % 256) + 256) % 256;
        psx.refreshDue += dr;
        let ds = null;
        if (match.s !== null && p.spuPhase !== null && p.spuPhase !== undefined) {
          ds = (((p.spuPhase - match.s) % 1536) + 1536) % 1536;
          psx.spu.catchUp(); psx.spu.tickCarry -= ds; psx.spu.schedule();
        }
        if (lock) Object.assign(lock, ds !== null ? { spu: true, phase: match.s } : { spu: false, phase: match.r }, { phi: match.phi, phiTol: tol });
        return { phase: p.phase, spuPhase: p.spuPhase, phi: p.phi, frames: p.frames, refreshShift: dr, spuShift: ds };
      };
    }
    if (opt('refresh-out')) fs.writeFileSync(opt('refresh-out'), JSON.stringify(await r.refresh()) + '\n');
    // --line-phase: the GPU's half-tick parity when the program's first instruction runs, read off the virtual console
    let accept = null;
    if (vc && opt('line-phase') !== undefined) {
      const want = +opt('line-phase'), psx = vc.psx, P = Object.getPrototypeOf(psx);
      let entry = 0, seen = -1;
      const orig = P.runFor;
      P.runFor = function (n, until) {
        if (entry && !this.watchPc) this.watchPc = entry | 0;
        const k = orig.call(this, n, until);
        if (entry && (this.pc | 0) === (entry | 0) && seen < 0) seen = this.gpu.linePhase;
        return k;
      };
      accept = (exe, done) => { const ok = seen === want; done.linePhase = seen; seen = -1; return ok; };
      const load = r.load.bind(r);
      r.load = (exe) => { entry = exe.readUInt32LE(0x10) >>> 0; seen = -1; return load(exe); };
    }
    const reps = Math.max(1, +opt('repeat', 1));
    const runOpts = { delay: +opt('delay', 1000), timeout: +opt('timeout', 600), out: opt('out'), lock, accept };
    const list = names.flatMap((n) => Array(reps).fill(n));
    let res;
    if (match) {   // a probe landing before each run (above), its reading kept with the run
      res = [];
      for (const n of list) { const probe = await match.land(); for (const x of await runTests(r, [n], runOpts)) res.push({ ...x, match: { r: match.r, s: match.s, phi: match.phi, probe } }); }
    } else res = await runTests(r, list, runOpts);
    if (flag('json')) { process.stdout.write(JSON.stringify(res) + '\n'); if (vc) vc.stop(); if (link.close) link.close(); process.exit(0); }
    for (const x of res) {
      if (!flag('raw')) console.log(`== ${x.name}: ${x.len} bytes, ${x.cycles} cycles, ${HOW[x.how] || 'how ' + x.how} (${x.secs.toFixed(1)} s)` +
        (x.frames !== undefined ? `, locked after ${x.frames} frames at refresh phase ${x.phase}` : '') +
        (x.spuPhase !== undefined ? `, SPU phase ${x.spuPhase}, spuRefresh ${x.spuRefresh}` : '') +
        (x.phi !== undefined ? `, phi ${x.phi === null ? 'unread' : x.phi.toFixed(4)}` : ''));
      if (!opt('out')) process.stdout.write(x.text.replace(/\r\n?/g, '\n'));
    }
    if (vc) vc.stop();
    if (link.close) link.close();
    process.exit(0);
  })().catch((e) => { console.error('hwrunner: ' + (e && e.stack || e)); process.exit(1); });
}
