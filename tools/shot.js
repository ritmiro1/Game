/* Рендер арены в PNG (canvas через @napi-rs/canvas) — визуальная проверка */
'use strict';
const { JSDOM } = require('jsdom');
const { createCanvas } = require('@napi-rs/canvas');
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(ROOT, 'dist/rezonans.html'), 'utf8');
const errors = [];
const dom = new JSDOM(html, {
  url: 'http://localhost:8080/', runScripts: 'dangerously', resources: 'usable', pretendToBeVisual: true,
  beforeParse(w) {
    Object.defineProperty(w.HTMLElement.prototype, 'clientWidth', { get() { return 1000; }, configurable: true });
    Object.defineProperty(w.HTMLElement.prototype, 'clientHeight', { get() { return 760; }, configurable: true });
    w.HTMLCanvasElement.prototype.getContext = function (t) {
      if (!this.__nc) this.__nc = createCanvas(this.__w || 300, this.__h || 150);
      return this.__nc.getContext('2d');
    };
    Object.defineProperty(w.HTMLCanvasElement.prototype, 'width', {
      get() { return this.__w || 300; },
      set(v) { this.__w = v; if (this.__nc) this.__nc.width = v; }, configurable: true
    });
    Object.defineProperty(w.HTMLCanvasElement.prototype, 'height', {
      get() { return this.__h || 150; },
      set(v) { this.__h = v; if (this.__nc) this.__nc.height = v; }, configurable: true
    });
    w.AudioContext = function () {
      const g = { gain: { value: 0, setValueAtTime() { }, exponentialRampToValueAtTime() { }, linearRampToValueAtTime() { } }, connect() { } };
      return {
        currentTime: 0, sampleRate: 44100, state: 'running', destination: {},
        createGain: () => g,
        createOscillator: () => ({ type: '', frequency: { setValueAtTime() { }, exponentialRampToValueAtTime() { }, value: 0 }, connect() { }, start() { }, stop() { } }),
        createBuffer: (a, l) => ({ getChannelData: () => new Float32Array(l) }),
        createBufferSource: () => ({ buffer: null, connect() { }, start() { } }),
        createBiquadFilter: () => ({ type: '', frequency: { setValueAtTime() { }, exponentialRampToValueAtTime() { }, value: 0 }, Q: { value: 0 }, connect() { } }),
        resume() { }
      };
    };
    w.addEventListener('error', e => errors.push('error: ' + (e.error && e.error.stack || e.message)));
  }
});
const w = dom.window;
const wait = ms => new Promise(r => setTimeout(r, ms));

function save(id, file) {
  const el = w.document.getElementById(id);
  if (!el || !el.__nc) { console.log('нет канвы', id); return; }
  const src = el.__nc;
  const out = createCanvas(src.width, src.height);
  const x = out.getContext('2d');
  const g = x.createLinearGradient(0, 0, out.width, out.height);
  g.addColorStop(0, '#0a1024'); g.addColorStop(1, '#04050b');
  x.fillStyle = g; x.fillRect(0, 0, out.width, out.height);
  x.drawImage(src, 0, 0);
  fs.writeFileSync(path.join(ROOT, 'dist', file), out.toBuffer('image/png'));
  console.log('✓ dist/' + file);
}

(async () => {
  await wait(1200);
  w.Game.start({ mode: 'bot', mapId: process.env.MAP || 'clover', factions: [0, 1], botLevel: 1, botW: w.RZ.BOTS[1].w, botName: 'Такт', names: ['Ты', 'Такт'] });
  await wait(1600);
  // выделим юнита, чтобы были подсветки
  const st = w.Game.state;
  const mine = [];
  for (let i = 0; i < w.RZ.MAXU; i++) if (st.uAlive[i] && st.uOwner[i] === 0 && st.uType[i] === 1) mine.push(i);
  if (mine.length) w.Game.selected = mine[0];
  w.GameUI.refresh();
  await wait(400);
  save('board', 'shot-board.png');
  save('bg', 'shot-bg.png');
  console.log('ошибок:', errors.length, errors.slice(0, 5));
  process.exit(0);
})();
