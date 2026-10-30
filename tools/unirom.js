'use strict';
/*
 * tools/unirom.js - UNIROM'S SERIAL PROTOCOL, THE PC SIDE (2026-09-29). What nops (NOTPSXSerial) says to a
 * PlayStation running Unirom, over any byte pipe: a real console's serial cable (tools/serialbridge.ps1 puts the COM
 * port on a socket), or this emulator running Unirom (tools/virtualconsole.js). tools/psxlink.js is the command line.
 *
 * WHERE THE PROTOCOL COMES FROM. nops' own source (github.com/JonathanDotCel/NOTPSXSerial: TransferLogic.cs, Args.cs,
 * CPU.cs), read 2026-09-29, and Unirom 8.0.K itself, disassembled in this emulator's RAM (the OpenBIOS cartridge's
 * Unirom - the FreePSXBoot image's version too):
 *   - A command is four ASCII letters. The menu's handler (801D7378h in 8.0.K) reads ONE byte per pass of its loop,
 *     ECHOES it, and shifts it into a four-byte window at 1F800000h, which it compares with its commands: FAST SLOW
 *     REST HEXE CALL JUMP SBIN DUMP HEXD DEBG SROM SEXE TEST HALT CONT HKRD HKWR HKEX UNHK SCOT LAND MCUP MCDN GAPS FLAT.
 *     PING is NOT among them: it is answered ("PONG") only by the kernel-resident debug handler that DEBG installs
 *     (nops lists /dbg as another name for /ping for that reason). So in the menu the echo is how one knows Unirom
 *     is there (Unirom.hello).
 *   - The answer is read as nops reads it: byte by byte through a four-character window, so an echo or a line of
 *     TTY before it does no harm. "OKV2" or "OKV3" instead of "OKAY" offers a newer protocol: the PC says "UPV2" /
 *     "UPV3" and the answer "OKAY" follows. 8.0.K offers V2 (its debug handler's words include OKV2).
 *   - Uploads (SBIN, SEXE, MCUP): address, length and a checksum of the whole (V1/V2: the sum of the bytes; V3: DJB2,
 *     c = ((c << 5) + c) ^ byte from 5381), then 2 KB chunks. From V2 on, after each chunk Unirom says "CHEK", the PC
 *     sends that chunk's byte sum, and Unirom says "MORE" (or "ERR!": send the chunk again). SEXE sends the EXE's
 *     2 KB header first, then its entry point, load address, the length after the header, and the checksum. MCUP
 *     sends the card's number in place of an address (nops' Command_MemcardUpload, read 2026-10-04).
 *   - DUMP: address and size, then the bytes; the PC says "MORE" after every 2 KB, and four bytes of checksum close
 *     it (either sum accepted, as nops does).
 *   - Registers (debug mode, halted): the kernel's TCB pointer at 80000110h, and 48 words there - stat, badv, r0-ra,
 *     pc, hi, lo, sr, cause, the branch-delay flag and eight more (CPU.cs's GPR enum). /setreg writes them back with
 *     SBIN; they take effect at CONT.
 * Every number is little-endian.
 *
 * IT RUNS IN THE PAGE TOO (M340). No Buffer: bytes are Uint8Arrays, and under node every array it hands back is a
 * Buffer over them, so psxlink keeps its Buffer methods. build.js inlines this file as UNIROMLIB for the page's "Your
 * PlayStation" box, which gives it a WebSerial port as the link.
 *
 * WHAT IT WILL NOT DO: SROM (flash a cartridge's EEPROM) is not implemented at all. MCUP (write a memory card) is
 * cardWrite, since M340, and only the page calls it, behind its own guards (the card read and kept first, an ordinary
 * card's directory required, the saves on it confirmed by name). Until M340 this file could not overwrite the card
 * that boots Unirom, and psxlink's command line still cannot.
 */

const REG_NAMES = ['stat', 'badv', 'r0', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6',
  't7', 's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra', 'pc', 'hi', 'lo',
  'sr', 'cause', 'bd', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7', 'x9'];
const ERRORS = {
  UNSP: 'not supported while Unirom is in debug mode',
  ONLY: 'only supported in debug mode (DEBG first)',
  HECK: 'the memory card could not be read',
};

class UniromError extends Error {}

/* Bytes handed back: a Buffer over them under node (psxlink's callers use its methods), the Uint8Array in a page. */
const bytesOut = typeof Buffer !== 'undefined' ? (u8) => Buffer.from(u8.buffer, u8.byteOffset, u8.byteLength) : (u8) => u8;
const latin1 = (b) => { let s = ''; for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode.apply(null, b.subarray(i, i + 8192)); return s; };
const fromLatin1 = (s) => { const b = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 0xFF; return b; };
const rd32 = (b, o = 0) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

/* Received bytes, in order, read from the front. */
class ByteQueue {
  constructor() { this.a = new Uint8Array(1 << 16); this.r = 0; this.w = 0; }
  get length() { return this.w - this.r; }
  push(buf) {
    if (this.w + buf.length > this.a.length) {
      const live = this.a.subarray(this.r, this.w);
      const b = new Uint8Array(Math.max(this.a.length, (live.length + buf.length) * 2));
      b.set(live); this.a = b; this.w = live.length; this.r = 0;
    }
    this.a.set(buf, this.w); this.w += buf.length;
  }
  shift() { return this.a[this.r++]; }
  take(n) { const b = this.a.slice(this.r, this.r + n); this.r += n; return b; }
}

const u32 = (v) => { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, v >>> 0, true); return bytesOut(b); };
const hex = (v) => (v >>> 0).toString(16).toUpperCase().padStart(8, '0') + 'h';

class Unirom {
  /* link: { write(Uint8Array), onData(fn) -> unsubscribe } */
  constructor(link, { idleMs = 3000 } = {}) {
    this.link = link;
    this.q = new ByteQueue();
    this.version = 1;
    this.idleMs = idleMs;
    this.wake = null;
    this.tty = [];                          // bytes that came while no command was waiting (a program's printf, echoes)
    this.off = link.onData((b) => { this.q.push(b); if (this.wake) { const w = this.wake; this.wake = null; w(); } });
  }
  close() { if (this.off) this.off(); }
  _write(...parts) {
    for (const p of parts) this.link.write(typeof p === 'string' ? fromLatin1(p) : p instanceof Uint8Array ? p.slice() : Uint8Array.from(p));
  }

  /* The clock the deadlines run on: the virtual console's own (link.now, 2026-10-20 - a deadline on the host's clock
   * ran out at a different emulated moment every run), else the host's. */
  _now() { return this.link.now ? this.link.now() : Date.now(); }
  _wait(ms) {
    // the virtual console: in its own time, so a wait that runs out runs out at the same place every run (2026-10-20)
    if (this.link.wait) { const n0 = this.q.length; return this.link.wait(ms, () => this.q.length > n0) ? Promise.resolve() : Promise.reject(new UniromError('timeout')); }
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => { this.wake = null; reject(new UniromError('timeout')); }, ms);
      this.wake = () => { clearTimeout(t); resolve(); };
    });
  }
  /* Everything received and not yet read: kept as TTY and returned. */
  drain() { const b = this.q.take(this.q.length); if (b.length) this.tty.push(b); return bytesOut(b); }
  /* Wait `ms` (or until `pattern` shows up in what arrives) and return what arrived, as text. */
  async listen(ms, pattern = null) {
    const end = this._now() + ms;
    let text = '';
    for (;;) {
      text += latin1(this.drain());
      if (pattern && pattern.test(text)) return text;
      const left = end - this._now();
      if (left <= 0) return text;
      try { await this._wait(left); } catch (e) { /* the time is up */ }
    }
  }

  async _read(n) {
    while (this.q.length < n) {
      try { await this._wait(this.idleMs); } catch (e) { throw new UniromError(`no data for ${this.idleMs} ms (${this.q.length} of ${n} bytes)`); }
    }
    return bytesOut(this.q.take(n));
  }
  async _token(want, ms, upgrade = false) {
    const end = this._now() + ms;
    let win = '';
    for (;;) {
      while (this.q.length) {
        win = (win + String.fromCharCode(this.q.shift())).slice(-4);
        if (upgrade && this.version === 1 && (win === 'OKV2' || win === 'OKV3')) {
          this.version = win === 'OKV3' ? 3 : 2;
          this._write(win === 'OKV3' ? 'UPV3' : 'UPV2');
          win = '';
          continue;
        }
        if (want.includes(win)) return win;
        if (ERRORS[win]) throw new UniromError(ERRORS[win]);
      }
      const left = end - this._now();
      if (left <= 0) throw new UniromError(`no ${want.join('/')} in ${ms} ms`);
      try { await this._wait(left); } catch (e) { /* loop and report */ }
    }
  }

  /* A command, as nops sends one: what came before is TTY, then the four letters, then the answer. */
  async challenge(cmd, expect = ['OKAY'], ms = 4000) {
    this.drain();
    this.version = 1;
    this._write(cmd);
    return this._token(expect, ms, true);
  }
  _sum(data) {
    if (this.version === 3) { let c = 5381; for (let i = 0; i < data.length; i++) c = (((c << 5) + c) ^ data[i]) >>> 0; return c; }
    let s = 0; for (let i = 0; i < data.length; i++) s = (s + data[i]) >>> 0; return s;
  }
  /* `progress(done, total)`, when given, after every chunk. */
  async _chunks(data, start = 0, progress = null) {
    for (let i = start; i < data.length; i += 2048) {
      const chunk = data.subarray(i, Math.min(i + 2048, data.length));
      for (let tries = 0; ; tries++) {
        this._write(chunk);
        if (this.version < 2) break;
        await this._token(['CHEK'], 8000);
        let s = 0; for (const b of chunk) s = (s + b) >>> 0;       // nops sends a byte sum per chunk at every version
        this._write(u32(s));
        if ((await this._token(['MORE', 'ERR!'], 8000)) === 'MORE') break;
        if (tries === 3) throw new UniromError(`chunk at +${i} refused four times`);
      }
      if (progress) progress(Math.min(i + 2048, data.length) - start, data.length - start);
    }
  }
  async _readChecked(size, progress = null) {
    const out = new Uint8Array(size);
    let got = 0, v2 = 0, v3 = 5381;
    while (got < size) {
      const n = Math.min(2048 - (got % 2048), size - got);
      const c = await this._read(n);
      out.set(c, got);
      for (let i = 0; i < c.length; i++) { v2 = (v2 + c[i]) >>> 0; v3 = (((v3 << 5) + v3) ^ c[i]) >>> 0; }
      got += n;
      if (got % 2048 === 0) this._write('MORE');
      if (progress) progress(got, size);
    }
    const ck = rd32(await this._read(4));
    if (ck !== v2 && ck !== v3) throw new UniromError(`checksum ${hex(ck)}, but the bytes sum to ${hex(v2)} (V2) / ${hex(v3)} (V3)`);
    return bytesOut(out);
  }

  /* Is Unirom listening? The menu echoes a byte it reads; the debug handler answers PING. */
  async hello(ms = 3000) {
    try { await this.challenge('PING', ['PONG'], Math.min(ms, 1500)); return 'debug'; } catch (e) { /* not in debug mode */ }
    const t = this.tty.map(latin1).join('') + latin1(this.drain());
    if (/PING/.test(t)) return 'menu';
    this._write('\n');                                          // a newline is no command: the echo alone answers
    const back = await this.listen(ms, /\n/);
    return /\n/.test(back) ? 'menu' : null;
  }

  async dump(addr, size, progress = null) { await this.challenge('DUMP'); this._write(u32(addr), u32(size)); return this._readChecked(size, progress); }
  async sendBin(addr, data) {
    await this.challenge('SBIN');
    this._write(u32(addr), u32(data.length), u32(this._sum(data)));
    await this._chunks(data);
  }
  async poke(addr, value, width = 4) {
    const b = new Uint8Array(width), v = new DataView(b.buffer);
    if (width === 1) v.setUint8(0, value & 0xFF); else if (width === 2) v.setUint16(0, value & 0xFFFF, true); else v.setUint32(0, value >>> 0, true);
    await this.sendBin(addr, b);
  }
  async runExe(exe, progress = null) {
    if (latin1(exe.subarray(0, 8)) !== 'PS-X EXE') throw new UniromError('not a PS-X EXE (no "PS-X EXE" header)');
    const buf = new Uint8Array(Math.ceil(exe.length / 2048) * 2048);
    buf.set(exe);
    await this.challenge('SEXE');
    const body = buf.subarray(2048);
    this._write(buf.subarray(0, 2048));
    this._write(buf.subarray(16, 20), buf.subarray(24, 28), u32(body.length), u32(this._sum(body)));
    await this._chunks(buf, 2048, progress);
  }
  async jump(addr) { await this.challenge('JUMP'); this._write(u32(addr)); }
  async call(addr) { await this.challenge('CALL'); this._write(u32(addr)); }
  /* DEBG never asks to upgrade (2026-10-05, the SCPH-5501's card): that Unirom (8.0.I, FreePSXBoot's 20210703
   * build) answers "OKV2" and then "OKAY" by itself ~64 ms later; an "UPV2" sent after that lands as a new command,
   * and the first run after it hung. DEBG carries nothing a newer protocol changes, so the plain "OKAY" is waited for,
   * as tools/consolerun.js's link does (two whole console runs through it). */
  async debug() {
    this.drain();
    this.version = 1;
    this._write('DEBG');
    await this._token(['OKAY'], 5000, false);
  }
  async ping(ms = 1500) { await this.challenge('PING', ['PONG'], ms); }
  async halt() { await this.challenge('HALT', ['HLTD']); }
  async cont() { await this.challenge('CONT'); }
  async reset() { await this.challenge('REST'); }
  async hook(kind, addr) {
    const cmd = { read: 'HKRD', write: 'HKWR', exec: 'HKEX' }[kind];
    if (!cmd) throw new UniromError('a hook is read, write or exec');
    await this.challenge(cmd, ['OKAY', 'PONG']);                // nops expects PONG for HKRD and OKAY for the others
    this._write(u32(addr));
  }
  async unhook() { await this.challenge('UNHK'); }
  async tcb() { return rd32(await this.dump(0x80000110, 4)); }
  async regs() {
    const at = await this.tcb();
    const raw = await this.dump(at, REG_NAMES.length * 4);
    const r = { _tcb: at };
    REG_NAMES.forEach((n, i) => { r[n] = rd32(raw, i * 4); });
    return r;
  }
  async setReg(name, value) {
    const i = REG_NAMES.indexOf(name.toLowerCase());
    if (i < 0) throw new UniromError('no register ' + name + ' (' + REG_NAMES.slice(2, 39).join(' ') + ')');
    const at = await this.tcb();
    await this.sendBin(at + i * 4, u32(value));
  }
  /* A menu command (the debug handler does not know it). Unirom reads the card into RAM - 1,024 sectors at a card's
   * pace, 13 s here at full speed - says "MCRD" and where it put it, and the PC DUMPs that (nops' Command_MemcardDownload). */
  async cardRead(slot, progress = null) {
    await this.challenge('MCDN', ['HLTD', 'OKAY']);
    this._write(u32(slot));
    await this._token(['MCRD'], 180000);
    const hdr = await this._read(8);
    return this.dump(rd32(hdr, 0), rd32(hdr, 4), progress);
  }
  /* A menu command too: the whole card (131,072 bytes) as an upload, the card's number where SBIN has its address
   * (nops' Command_MemcardUpload). Unirom takes it into RAM, then writes it sector by sector, saying "Writing sector
   * N of 1024 (P%)" every 16 sectors - and NOTHING when it is done (8.0.K on the virtual console, 2026-10-04: the last
   * line is sector 1008's, then it writes the last 16 and its menu runs again). So the end is the menu's echo of a
   * byte sent after the last line. With no card in the slot it never starts: no line at all, and Unirom answers
   * nothing until the console is reset - so a caller reads the card first (a read of an empty slot fails at once,
   * HECK). NO OTHER GUARD HERE: the page's consWriteCard keeps the card and refuses one that is not an ordinary card.
   * `progress(phase, done, total)`: 'send' in bytes, then 'write' in sectors. */
  async cardWrite(slot, image, progress = null, { startMs = 15000, lineMs = 30000 } = {}) {
    if (image.length !== 131072) throw new UniromError(`a memory card is 131,072 bytes, not ${image.length}`);
    await this.challenge('MCUP');
    this._write(u32(slot), u32(image.length), u32(this._sum(image)));
    await this._chunks(image, 0, progress ? (d, t) => progress('send', d, t) : null);
    let text = '', last = -1, total = 1024, at = this._now();
    for (;;) {
      const got = latin1(this.drain());
      text = (text + got).slice(-400);
      const re = /Writing sector (\d+) of (\d+)[^\n]*\n/g;
      let m, moved = false;
      while ((m = re.exec(text))) { if (+m[1] > last) { last = +m[1]; total = +m[2]; moved = true; } }
      if (moved) { at = this._now(); if (progress) progress('write', Math.min(total, last + 16), total); }
      if (last >= 0 && last + 16 >= total) break;
      const wait = (last < 0 ? startMs : lineMs) - (this._now() - at);
      if (wait <= 0) {
        throw new UniromError(last < 0 ? 'Unirom took the card but never started writing it (is a card in that slot?) - the console needs a reset'
          : `Unirom stopped writing after sector ${last} of ${total}`);
      }
      try { await this._wait(wait); } catch (e) { /* checked above */ }
    }
    if (!(await this.echo(20000))) throw new UniromError(`the card was written to sector ${last} of ${total}, but the menu did not answer after it`);
  }
  /* Is the menu's loop running? It echoes every byte it reads: send one and wait for it back. */
  async echo(ms = 3000, ch = '#') {
    this.drain();
    this._write(ch);
    const end = this._now() + ms;
    for (;;) {
      if (latin1(this.drain()).includes(ch)) return true;
      const left = end - this._now();
      if (left <= 0) return false;
      try { await this._wait(left); } catch (e) { /* the time is up */ }
    }
  }
}

/* "80010000", "0x80010000", "80010000h" */
function parseNum(s) {
  const t = String(s).trim().toLowerCase();
  const m = /^(?:0x)?([0-9a-f]+)h?$/.exec(t);
  if (!m) throw new UniromError('not a hex number: ' + s);
  return parseInt(m[1], 16) >>> 0;
}
function hexdump(buf, base) {
  const lines = [];
  for (let i = 0; i < buf.length; i += 16) {
    const row = buf.subarray(i, i + 16);
    const h = [...row].map((b) => b.toString(16).padStart(2, '0')).join(' ');
    const a = [...row].map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : '.')).join('');
    lines.push(`${((base + i) >>> 0).toString(16).padStart(8, '0')}  ${h.padEnd(47)}  ${a}`);
  }
  return lines.join('\n');
}
function formatRegs(r) {
  const n = REG_NAMES.slice(2, 34);
  const rows = [];
  for (let i = 0; i < n.length; i += 4) rows.push(n.slice(i, i + 4).map((k) => `${k.padStart(2)} ${(r[k] >>> 0).toString(16).padStart(8, '0')}`).join('   '));
  rows.push(`pc ${(r.pc >>> 0).toString(16).padStart(8, '0')}   hi ${(r.hi >>> 0).toString(16).padStart(8, '0')}   lo ${(r.lo >>> 0).toString(16).padStart(8, '0')}   sr ${(r.sr >>> 0).toString(16).padStart(8, '0')}`);
  rows.push(`cause ${(r.cause >>> 0).toString(16).padStart(8, '0')}   badv ${(r.badv >>> 0).toString(16).padStart(8, '0')}   in a delay slot: ${r.bd ? 'yes' : 'no'}   (TCB at ${(r._tcb >>> 0).toString(16)})`);
  return rows.join('\n');
}

module.exports = { Unirom, UniromError, REG_NAMES, parseNum, hexdump, formatRegs, u32, latin1, rd32 };
