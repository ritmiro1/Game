/* Сборка одностраничной версии игры (dist/rezonans.html) + C-заголовок для лаунчера */
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const OUT_DIR = path.join(ROOT, 'dist');
if (!fs.existsSync(OUT_DIR)) fs.mkdirSync(OUT_DIR, { recursive: true });

function read(p) { return fs.readFileSync(path.join(ROOT, p), 'utf8'); }

const html = read('web/index.html');
const css = read('web/css/style.css');

const scripts = [
  '../shared/content.js',
  '../shared/engine.js',
  '../shared/ai.js',
  'js/audio.js',
  'js/render.js',
  'js/ui.js',
  'js/game.js',
  'js/net.js',
  'js/app.js'
];
const workerParts = ['shared/content.js', 'shared/engine.js', 'shared/ai.js', 'web/js/worker-glue.js'];
const workerSrc = workerParts.map(read).join('\n;\n');

let out = html;
out = out.replace('<link rel="stylesheet" href="css/style.css">', '<style>\n' + css + '\n</style>');

// воркер-исходник (для blob-воркера в одностраничной версии)
out = out.replace('<script id="worker-src" type="text/plain"></script>',
  '<script>window.__WORKER_SRC = ' + JSON.stringify(workerSrc) + ';</script>');

// инлайн скриптов
scripts.forEach(function (src) {
  const tag = '<script src="' + src + '"></script>';
  const code = fs.readFileSync(path.join(ROOT, 'web', src), 'utf8');
  if (out.indexOf(tag) < 0) throw new Error('не найден тег скрипта: ' + tag);
  // защита от </script> внутри кода
  const safe = code.replace(/<\/script>/g, '<\\/script>');
  out = out.replace(tag, '<script>\n' + safe + '\n</script>');
});

const target = path.join(OUT_DIR, 'rezonans.html');
fs.writeFileSync(target, out);
console.log('✓ dist/rezonans.html  ' + (out.length / 1024).toFixed(1) + ' КБ');

/* ---------------- C-заголовок для лаунчера ---------------- */
const bytes = Buffer.from(out, 'utf8');
const chunks = [];
const CHUNK = 24;
for (let i = 0; i < bytes.length; i += CHUNK) {
  const slice = bytes.slice(i, i + CHUNK);
  let line = '  ';
  for (let j = 0; j < slice.length; j++) line += '0x' + slice[j].toString(16).padStart(2, '0') + ',';
  chunks.push(line);
}
const header = '/* автогенерировано tools/build.js — не редактировать */\n' +
  '#ifndef GAME_HTML_H\n#define GAME_HTML_H\n' +
  'static const unsigned int GAME_HTML_LEN = ' + bytes.length + ';\n' +
  'static const unsigned char GAME_HTML[] = {\n' + chunks.join('\n') + '\n};\n' +
  '#endif\n';
fs.writeFileSync(path.join(ROOT, 'native/game_html.h'), header);
console.log('✓ native/game_html.h  ' + (bytes.length / 1024).toFixed(1) + ' КБ данных');
