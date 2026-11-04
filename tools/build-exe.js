/* Сборка Rezonans.exe: zig cc → Windows PE (кросс-компиляция) */
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const ZIG = process.env.ZIG || findZig();
function findZig() {
  const cands = [
    '/tmp/zv/lib/python3.11/site-packages/ziglang/zig',
    'zig'
  ];
  for (const c of cands) { try { if (c === 'zig' || fs.existsSync(c)) return c; } catch (e) { } }
  return 'zig';
}

// сначала пересобираем ассеты
require('./build.js');

const out = path.join(ROOT, 'download', 'Rezonans.exe');
if (!fs.existsSync(path.dirname(out))) fs.mkdirSync(path.dirname(out), { recursive: true });

const args = [
  'cc', '-target', 'x86_64-windows-gnu',
  path.join(ROOT, 'native', 'launcher.c'),
  '-o', out,
  '-O2', '-s',
  '-Wl,--subsystem,windows',
  '-lws2_32', '-lshell32', '-luser32'
];
console.log('zig ' + args.join(' '));
execFileSync(ZIG, args, { stdio: 'inherit' });
const st = fs.statSync(out);
console.log('✓ ' + path.relative(ROOT, out) + '  ' + (st.size / 1024).toFixed(0) + ' КБ');
