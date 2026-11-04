/* Автопрогон партий через настоящий UI-слой (jsdom) — ищем ошибки в бою */
'use strict';
const { JSDOM } = require('jsdom');
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..');
const errors = [];
function fakeCtx() {
  const grad = { addColorStop() { } };
  const c = { globalAlpha: 1, fillStyle: '', strokeStyle: '', lineWidth: 1, font: '', textAlign: '', textBaseline: '', shadowColor: '', shadowBlur: 0, lineDashOffset: 0 };
  ['save', 'restore', 'beginPath', 'closePath', 'moveTo', 'lineTo', 'arc', 'fill', 'stroke', 'fillRect', 'clearRect', 'translate', 'rotate', 'scale', 'setTransform', 'clip', 'setLineDash', 'fillText'].forEach(k => c[k] = () => { });
  c.measureText = () => ({ width: 10 });
  c.createLinearGradient = () => grad; c.createRadialGradient = () => grad; c.drawImage = () => { };
  return c;
}
const html = fs.readFileSync(path.join(ROOT, process.env.TARGET || 'dist/rezonans.html'), 'utf8');
const dom = new JSDOM(html, {
  url: 'http://localhost:8080/', runScripts: 'dangerously', resources: 'usable', pretendToBeVisual: true,
  beforeParse(w) {
    Object.defineProperty(w.HTMLElement.prototype, 'clientWidth', { get() { return 960; }, configurable: true });
    Object.defineProperty(w.HTMLElement.prototype, 'clientHeight', { get() { return 720; }, configurable: true });
    w.HTMLCanvasElement.prototype.getContext = () => fakeCtx();
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
    w.onunhandledrejection = e => errors.push('reject: ' + (e.reason && e.reason.stack || e.reason));
    const ce = w.console.error;
    w.console.error = function () { errors.push('console.error: ' + Array.prototype.join.call(arguments, ' ')); };
  }
});
const w = dom.window;
const wait = ms => new Promise(r => setTimeout(r, ms));

function clickAt(cell) {
  const cv = w.document.getElementById('board');
  const lay = w.RZRender.layout();
  if (!lay) return;
  const p = lay.cells[cell];
  const r = cv.getBoundingClientRect();
  cv.dispatchEvent(new w.MouseEvent('click', { bubbles: true, clientX: r.left + lay.ox + p.x, clientY: r.top + lay.oy + p.y }));
}

async function autoPlay(label, cfg, maxSteps) {
  w.Game.start(cfg);
  w.RZRender.setSpeed(30);
  let steps = 0;
  const modalSeen = { v: false };
  while (steps++ < maxSteps) {
    await wait(5);
    const G = w.Game;
    if (!G.active) break;
    if (G.busy) continue;
    if (!w.GameUI.isMyTurn()) { if (steps % 400 === 0) console.log('   [жду] ход', G.state.turn, 'сторона', G.state.side); continue; }
    const acts = G.allMoves.filter(a => a.t !== 'end');
    const useEnd = !acts.length || Math.random() < 0.15;
    if (useEnd) {
      w.document.getElementById('btn-endturn').dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
      await wait(15);
      continue;
    }
    const a = acts[Math.floor(Math.random() * acts.length)];
    if (a.t === 'move') { clickAt(w.Game.state.uCell[a.u]); await wait(4); clickAt(a.to); }
    else if (a.t === 'summon') { const b = w.document.querySelector('[data-ab="summon"]'); if (b) { b.dispatchEvent(new w.MouseEvent('click', { bubbles: true })); await wait(4); clickAt(a.to); } }
    else if (a.t === 'atk') { clickAt(w.Game.state.uCell[a.u]); await wait(4); clickAt(w.Game.state.uCell[a.target]); }
    else if (a.t === 'ab') {
      clickAt(w.Game.state.uCell[a.u]); await wait(4);
      const btn = w.document.querySelector('[data-ab="ability"]');
      if (btn && !btn.disabled) {
        btn.dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
        await wait(6);
        if (a.k === 'barrier') clickAt(a.to);
        else if (a.k === 'mark' || a.k === 'harpoon') clickAt(w.Game.state.uCell[a.target]);
      }
    }
    await wait(10);
  }
  for (let k = 0; k < 60 && !w.UI.modalOpen(); k++) await wait(50);
  const s = w.Game.state;
  const ended = !!w.UI && w.UI.modalOpen();
  console.log(`  ${label}: ходов ${s.turn}, победитель ${s.winner}, шагов ${steps}, финал ${ended ? 'показан' : 'НЕ ПОКАЗАН'}`);
  if (!ended) errors.push(label + ': модалка результата не появилась');
  w.UI.closeModal();
  w.Game.quit(true);
  await wait(60);
}

(async () => {
  await wait(1200);
  console.log('режимы:');
  const C = w.RZ;
  await autoPlay('бот (Бритва)', { mode: 'bot', mapId: 'rift', factions: [0, 2], botLevel: 1, botW: C.BOTS[1].w, botName: 'Такт', names: ['Тест', 'Такт'] }, 20000);
  await autoPlay('кампания 1', { mode: 'campaign', mapId: 'core', factions: [0, 0], botLevel: 1, botW: C.BOTS[0].w, botName: 'Искра', chapter: C.CAMPAIGN[0], names: ['Тест', 'Искра'] }, 20000);
  await autoPlay('задача 1', { mode: 'puzzle', puzzle: C.PUZZLES[0], side: 0 }, 200);
  await autoPlay('хотсит', { mode: 'hotseat', mapId: 'clover', factions: [1, 1], names: ['А', 'Б'] }, 9000);
  console.log('ошибок:', errors.length);
  [...new Set(errors)].slice(0, 20).forEach(e => console.log('•', e));
  process.exit(errors.length ? 1 : 0);
})();
