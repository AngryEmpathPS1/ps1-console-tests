'use strict';
/*
 * hwtests/build.js — build our own PS-EXE tests with ps1-tests' toolchain.
 *
 * The toolchain is a Docker image (jaczekanski/psn00bsdk:latest, 247 MB), which
 * is how ps1-tests itself builds - see its build.sh. Nothing is installed on the
 * host; `docker rmi jaczekanski/psn00bsdk` removes it again.
 *
 * Our tests need ps1-tests' `common/` library and its two makefiles, so this
 * assembles a build root: a ps1-tests checkout, plus every directory under
 * hwtests/ that has a Makefile, placed one level deep (hw/<name>) so the
 * `../../common-test.mk` include resolves exactly as it does for their tests.
 *
 * The build root must be free of spaces: Docker volume arguments do not survive
 * one. Set PSX_HWBUILD if your temporary folder has a space in it.
 *
 *   node hwtests/build.js              every test
 *   node hwtests/build.js store-cost   just that one
 *   node hwtests/build.js --clean      throw the build root away first
 */
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const REPO = path.resolve(__dirname, '..');
const SRC = process.env.PS1_TESTS ? path.resolve(process.env.PS1_TESTS) : path.join(REPO, '..', 'ps1-tests');
const ROOT = process.env.PSX_HWBUILD || path.join(require('os').tmpdir(), 'ps1-tests-build');
const IMAGE = 'jaczekanski/psn00bsdk:latest';
// PSX_HWOUT: build somewhere else - a changed test beside the programs a console run is sending (2026-10-30)
const OUT = process.env.PSX_HWOUT ? path.resolve(process.env.PSX_HWOUT) : path.join(REPO, 'hwtests', 'build');

const args = process.argv.slice(2);
const only = args.filter((a) => !a.startsWith('--'));

/*
 * Hand-rolled, because fs.cpSync CRASHES node on this tree - not an exception,
 * a 0xC0000409 stack-buffer-overrun with no output at all, which reads exactly
 * like "the script printed nothing" until you check the exit code. Node
 * v24.11.1, Windows, ~300 files. readdir + copyFileSync does the same job.
 */
function copyDir(src, dst) {
  fs.mkdirSync(dst, { recursive: true });
  for (const e of fs.readdirSync(src, { withFileTypes: true })) {
    const a = path.join(src, e.name), b = path.join(dst, e.name);
    if (e.isDirectory()) copyDir(a, b);
    else if (e.isFile()) fs.copyFileSync(a, b);
  }
}

function docker(makeArgs) {
  return execFileSync('docker', ['run', '--rm', '-v', ROOT + ':/build', IMAGE, 'make', ...makeArgs],
    { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], env: { ...process.env, MSYS_NO_PATHCONV: '1' } });
}

if (!fs.existsSync(SRC)) {
  console.error('No ps1-tests source at ' + SRC + '\n(clone https://github.com/JaCzekanski/ps1-tests beside this repository, or set PS1_TESTS)');
  process.exit(2);
}
if (args.includes('--clean') && fs.existsSync(ROOT)) fs.rmSync(ROOT, { recursive: true, force: true });

/* 1. ps1-tests, for common/ and the makefiles */
fs.mkdirSync(ROOT, { recursive: true });
copyDir(SRC, ROOT);

/* 2. our tests, one level deep so ../../common-test.mk resolves */
const mine = fs.readdirSync(path.join(REPO, 'hwtests'), { withFileTypes: true })
  .filter((d) => d.isDirectory() && fs.existsSync(path.join(REPO, 'hwtests', d.name, 'Makefile')))
  .map((d) => d.name)
  .filter((n) => !only.length || only.includes(n));
if (!mine.length) { console.error('no tests to build' + (only.length ? ' matching ' + only.join(', ') : '')); process.exit(2); }
for (const n of mine) copyDir(path.join(REPO, 'hwtests', n), path.join(ROOT, 'hw', n));

/* 3. libcommon first - a test that links before it exists fails with
 *    "cannot find -lcommon", which is what the first attempt at this did. */
console.log('building libcommon...');
try { docker(['-C', 'common']); } catch (e) { console.error((e.stdout || '') + (e.stderr || '')); process.exit(1); }

fs.mkdirSync(OUT, { recursive: true });
let ok = 0;
for (const n of mine) {
  process.stdout.write(('  ' + n).padEnd(24));
  try {
    docker(['-C', 'hw/' + n]);
    const exe = fs.readdirSync(path.join(ROOT, 'hw', n)).find((f) => f.endsWith('.exe'));
    if (!exe) throw new Error('no .exe produced');
    const dst = path.join(OUT, exe);
    fs.copyFileSync(path.join(ROOT, 'hw', n, exe), dst);
    console.log('OK  ' + path.relative(REPO, dst) + '  ' + fs.statSync(dst).size + ' bytes');
    ok++;
  } catch (e) {
    console.log('FAILED');
    console.error((e.stdout || '') + (e.stderr || String(e.message)));
  }
}
console.log(`\n${ok}/${mine.length} built into ${path.relative(REPO, OUT)}`);
process.exitCode = ok === mine.length ? 0 : 1;
