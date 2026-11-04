/* End-to-end: два «браузера» (jsdom) играют онлайн через настоящий сервер */
'use strict';
const { JSDOM } = require('jsdom');
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(ROOT, 'dist/rezonans.html'), 'utf8');
const errors = [];
function fakeCtx() {
  const grad = { addColorStop() { } };
  const c = { globalAlpha: 1, fillStyle: '', strokeStyle: '', lineWidth: 1, font: '', textAlign: '', textBaseline: '', shadowColor: '', shadowBlur: 0, lineDashOffset: 0 };
  ['save', 'restore', 'beginPath', 'closePath', 'moveTo', 'lineTo', 'arc', 'fill', 'stroke', 'fillRect', 'clearRect', 'translate', 'rotate', 'scale', 'setTransform', 'clip', 'setLineDash', 'fillText'].forEach(k => c[k] = () => { });
  c.measureText = () => ({ width: 10 });
  c.createLinearGradient = () => grad; c.createRadialGradient = () => grad; c.drawImage = () => { };
  return c;
}
function makeClient(tag) {
  const dom = new JSDOM(html, {
    url: 'http://localhost:8080/', runScripts: 'dangerously', resources: 'usable', pretendToBeVisual: true,
    beforeParse(w) {
      Object.defineProperty(w.HTMLElement.prototype, 'clientWidth', { get() { return 960; }, configurable: true });
      Object.defineProperty(w.HTMLElement.prototype, 'clientHeight', { get() { return 700; }, configurable: true });
      w.HTMLCanvasElement.prototype.getContext = () => fakeCtx();
      w.AudioContext = function () {
        const g = { gain: { value: 0, setValueAtTime() { }, exponentialRampToValueAtTime() { }, linearRampToValueAtTime() { } }, connect() { } };
        return {
          currentTime: 0, sampleRate: 44100, state: 'running', destination: {}, createGain: () => g,
          createOscillator: () => ({ type: '', frequency: { setValueAtTime() { }, exponentialRampToValueAtTime() { }, value: 0 }, connect() { }, start() { }, stop() { } }),
          createBuffer: (a, l) => ({ getChannelData: () => new Float32Array(l) }),
          createBufferSource: () => ({ buffer: null, connect() { }, start() { } }),
          createBiquadFilter: () => ({ type: '', frequency: { setValueAtTime() { }, exponentialRampToValueAtTime() { }, value: 0 }, Q: { value: 0 }, connect() { } }),
          resume() { }
        };
      };
      w.addEventListener('error', e => errors.push(tag + ': ' + (e.error && e.error.stack || e.message)));
      w.console.error = (...a) => errors.push(tag + ' console.error: ' + a.join(' '));
    }
  });
  return dom.window;
}
const wait = ms => new Promise(r => setTimeout(r, ms));
function click(w, sel) {
  const e = w.document.querySelector(sel);
  if (!e) { errors.push('нет ' + sel); return false; }
  e.dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
  return true;
}
function clickCell(w, cell) {
  const cv = w.document.getElementById('board');
  const lay = w.RZRender.layout();
  const p = lay.cells[cell];
  const r = cv.getBoundingClientRect();
  cv.dispatchEvent(new w.MouseEvent('click', { bubbles: true, clientX: r.left + lay.ox + p.x, clientY: r.top + lay.oy + p.y }));
}
async function playOne(w, who) {
  const G = w.Game;
  if (!G.allMoves.length) return false;
  const acts = G.allMoves.filter(a => a.t !== 'end');
  const a = acts[Math.floor(Math.random() * acts.length)];
  if (!a) return false;
  if (a.t === 'move') { clickCell(w, G.state.uCell[a.u]); await wait(30); clickCell(w, a.to); }
  else if (a.t === 'atk') { clickCell(w, G.state.uCell[a.u]); await wait(30); clickCell(w, G.state.uCell[a.target]); }
  else return false;
  console.log('   ' + who + ': ' + a.t + ' → ход ' + G.state.turn);
  return true;
}

(async () => {
  const A = makeClient('A'), B = makeClient('B');
  await wait(1200);
  A.UI.Store.data.name = 'Альфа'; A.UI.Store.save();
  B.UI.Store.data.name = 'Бета'; B.UI.Store.save();
  A.document.getElementById('inp-server').value = 'ws://localhost:8080';
  B.document.getElementById('inp-server').value = 'ws://localhost:8080';
  A.UI.go('online'); B.UI.go('online');
  click(A, '#btn-reconnect'); await wait(300);
  click(B, '#btn-reconnect'); await wait(600);
  console.log('подключение A/B:', A.Net.connected, B.Net.connected);
  if (!A.Net.connected || !B.Net.connected) { errors.push('нет подключения к серверу'); }
  click(A, '#btn-create-room');
  await wait(600);
  const joinBtn = B.document.querySelector('[data-join]');
  if (!joinBtn) errors.push('B не увидел лобби');
  else joinBtn.dispatchEvent(new B.MouseEvent('click', { bubbles: true }));
  await wait(900);
  console.log('режимы:', A.Game.mode, B.Game.mode, '| стороны:', A.Game.mySide, B.Game.mySide);
  if (A.Game.mode !== 'online' || B.Game.mode !== 'online') errors.push('дуэль не началась в клиентах');

  A.RZRender.setSpeed(40); B.RZRender.setSpeed(40);
  let turns = 0;
  for (let i = 0; i < 14; i++) {
    const w = A.Game.state.side === A.Game.mySide ? A : B;
    const other = w === A ? B : A;
    const before = other.Game.state ? JSON.stringify(other.RZ.encode(other.Game.state)) : '';
    const ok = await playOne(w, w === A ? 'A' : 'B');
    if (!ok) { click(w, '#btn-endturn'); }
    await wait(420);
    const after = other.Game.state ? JSON.stringify(other.RZ.encode(other.Game.state)) : '';
    if (before && before === after) errors.push('состояние у соперника не изменилось после хода');
    turns++;
  }
  console.log('сделано ходов:', turns);
  console.log('ход у A:', A.Game.state.turn, 'у B:', B.Game.state.turn,
    '| ядро у B:', B.Game.state.uHp[B.Game.state.uCell.length ? 0 : 0]);
  // сверка состояний
  const same = JSON.stringify(A.RZ.encode(A.Game.state)) === JSON.stringify(B.RZ.encode(B.Game.state));
  console.log('состояния совпадают:', same);
  if (!same) errors.push('рассинхрон состояний между клиентами');
  console.log(errors.length ? 'ОШИБКИ:\n' + [...new Set(errors)].join('\n') : '✓ онлайн-дуэль работает');
  process.exit(errors.length ? 1 : 0);
})();
