/* Дымовой тест клиента в jsdom: ищем ошибки времени выполнения и прогоняем сценарии */
'use strict';
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const errors = [];

function fakeCtx() {
  const grad = { addColorStop() { } };
  const ctx = {
    canvas: null, globalAlpha: 1, fillStyle: '', strokeStyle: '', lineWidth: 1, font: '', textAlign: '',
    textBaseline: '', shadowColor: '', shadowBlur: 0, lineDashOffset: 0,
    save() { }, restore() { }, beginPath() { }, closePath() { }, moveTo() { }, lineTo() { }, arc() { },
    fill() { }, stroke() { }, fillRect() { }, clearRect() { }, translate() { }, rotate() { }, scale() { },
    setTransform() { }, clip() { }, setLineDash() { }, fillText() { }, measureText() { return { width: 10 }; },
    createLinearGradient() { return grad; }, createRadialGradient() { return grad; }, drawImage() { }
  };
  return ctx;
}

const TARGET = process.env.TARGET || 'web/index.html';
const html = fs.readFileSync(path.join(ROOT, TARGET), 'utf8');
const dom = new JSDOM(html, {
  url: 'http://localhost:8080/web/index.html',
  runScripts: 'dangerously',
  resources: 'usable',
  pretendToBeVisual: true,
  beforeParse(window) {
    Object.defineProperty(window.HTMLElement.prototype, 'clientWidth', { get() { return 960; }, configurable: true });
    Object.defineProperty(window.HTMLElement.prototype, 'clientHeight', { get() { return 720; }, configurable: true });
    window.HTMLCanvasElement.prototype.getContext = function () { return fakeCtx(); };
    window.AudioContext = function () {
      return {
        currentTime: 0, sampleRate: 44100, state: 'running', destination: {},
        createGain: () => ({ gain: { value: 0, setValueAtTime() { }, exponentialRampToValueAtTime() { }, linearRampToValueAtTime() { } }, connect() { } }),
        createOscillator: () => ({ type: '', frequency: { setValueAtTime() { }, exponentialRampToValueAtTime() { }, value: 0 }, connect() { }, start() { }, stop() { } }),
        createBuffer: (c, l) => ({ getChannelData: () => new Float32Array(l) }),
        createBufferSource: () => ({ buffer: null, connect() { }, start() { } }),
        createBiquadFilter: () => ({ type: '', frequency: { setValueAtTime() { }, exponentialRampToValueAtTime() { }, value: 0 }, Q: { value: 0 }, connect() { } }),
        resume() { }
      };
    };
    window.addEventListener('error', e => errors.push('window.onerror: ' + (e.error && e.error.stack || e.message)));
    window.onunhandledrejection = e => errors.push('unhandled rejection: ' + (e.reason && e.reason.stack || e.reason));
    const origError = window.console.error;
    window.console.error = function () { errors.push('console.error: ' + Array.prototype.join.call(arguments, ' ')); origError.apply(window.console, arguments); };
    window.scrollTo = () => { };
  }
});

const w = dom.window;

function wait(ms) { return new Promise(r => setTimeout(r, ms)); }
function click(sel) {
  const e = w.document.querySelector(sel);
  if (!e) { errors.push('нет элемента ' + sel); return null; }
  e.dispatchEvent(new w.MouseEvent('click', { bubbles: true, clientX: 400, clientY: 300 }));
  return e;
}
function clickCell(idx) {
  const cv = w.document.getElementById('board');
  const lay = w.RZRender.layout();
  if (!lay) { errors.push('нет раскладки доски'); return; }
  const p = lay.cells[idx];
  const r = cv.getBoundingClientRect();
  cv.dispatchEvent(new w.MouseEvent('click', { bubbles: true, clientX: r.left + lay.ox + p.x, clientY: r.top + lay.oy + p.y }));
}

(async function () {
  await wait(1400);
  console.log('— модули:', ['RZ', 'RZRender', 'UI', 'Game', 'Net', 'App'].map(k => k + '=' + (typeof w[k])).join(' '));
  console.log('— экран:', w.UI.current);

  // старт бота
  click('[data-go="bots"]');
  await wait(200);
  click('[data-bot="0"]');
  await wait(900);
  console.log('— игра активна:', w.Game.active, 'режим:', w.Game.mode, 'ход:', w.Game.state && w.Game.state.turn);

  // пара кликов по арене
  const mv = w.RZ.getMoves(w.Game.state);
  console.log('— доступных действий:', mv.length, 'из них на моей стороне:', w.Game.allMoves.length);
  for (let k = 0; k < 6; k++) {
    if (!w.Game.allMoves.length || w.Game.busy) break;
    const mine = w.Game.allMoves.filter(a => a.t === 'move' || a.t === 'atk');
    const a = mine[Math.floor(Math.random() * mine.length)];
    if (!a) break;
    const target = a.t === 'move' ? a.to : w.Game.state.uCell[a.target];
    if (a.u >= 0) clickCell(w.Game.state.uCell[a.u]);
    await wait(120);
    clickCell(target);
    await wait(700);
    console.log('  клик →', a.t, '| ход', w.Game.state.turn, '| од', w.Game.state.od, '| busy', w.Game.busy);
  }

  // завершить ход
  click('#btn-endturn');
  await wait(1500);
  console.log('— после конца хода: ход', w.Game.state.turn, 'сторона', w.Game.state.side);

  // экраны
  w.Game.quit(true);
  for (const s of ['campaign', 'puzzles', 'rules', 'online', 'profile', 'menu']) {
    w.UI.go(s);
    await wait(80);
  }
  console.log('— экраны переключены, ошибок:', errors.length);
  if (errors.length) {
    console.log('\n=== ОШИБКИ ===');
    [...new Set(errors)].slice(0, 25).forEach(e => console.log('•', e));
    process.exit(1);
  } else {
    console.log('✓ ошибок нет');
    process.exit(0);
  }
})();
