const fs = require('node:fs');
const vm = require('node:vm');
const test = require('node:test');
const assert = require('node:assert/strict');
const code = fs.readFileSync(__dirname + '/player.js', 'utf8');

function setup(connection) {
  class Element {
    constructor() { this.listeners = {}; this.textContent = ''; this.hidden = true; this.checked = true; this.value = 'hd'; }
    addEventListener(type, fn, options) { (this.listeners[type] ||= []).push({fn, once: options && options.once}); }
    emit(type) {
      const listeners = (this.listeners[type] || []).slice();
      this.listeners[type] = (this.listeners[type] || []).filter(item => !item.once);
      listeners.forEach(item => item.fn());
    }
  }
  const nodes = Object.fromEntries(['playback-config', 'player', 'quality', 'automatic', 'status', 'error'].map(name => [name, new Element()]));
  nodes['playback-config'].textContent = JSON.stringify({sources: {hd: {src: '/watch.mp4?v=abc', label: 'HD'}, eco: {src: '/economy.mp4?v=def', label: 'Economy'}}});
  const video = nodes.player;
  Object.assign(video, {currentTime: 12, duration: 50.67, paused: false, ended: false, seeking: false, readyState: 2, src: '/watch.mp4?v=abc', stats: {totalVideoFrames: 0, droppedVideoFrames: 0}});
  video.pause = () => { video.paused = true; video.emit('pause'); };
  video.play = () => { video.paused = false; video.emit('playing'); return Promise.resolve(); };
  video.load = () => {};
  video.getVideoPlaybackQuality = () => video.stats;
  const document = new Element();
  document.hidden = false;
  document.getElementById = name => nodes[name];
  let next = 0;
  const timers = new Map(), intervals = new Map();
  const window = {
    setTimeout: fn => { timers.set(++next, fn); return next; },
    clearTimeout: id => timers.delete(id),
    setInterval: fn => { intervals.set(++next, fn); return next; },
    clearInterval: id => intervals.delete(id),
  };
  vm.runInNewContext(code, {document, navigator: {connection}, window, Number, JSON, console});
  return {nodes, video, document, timers, intervals, fireTimeouts() { const callbacks = [...timers.values()]; timers.clear(); callbacks.forEach(fn => fn()); }, sample() { [...intervals.values()].forEach(fn => fn()); }};
}

test('normal playback uses only the same-origin HD source', () => {
  const app = setup();
  assert.equal(app.video.src, '/watch.mp4?v=abc');
  assert.equal(app.nodes.quality.value, 'hd');
});

test('save-data starts with the economy copy without autoplay', () => {
  const app = setup({saveData: true});
  assert.equal(app.video.src, '/economy.mp4?v=def');
  app.video.emit('loadedmetadata');
  assert.equal(app.video.paused, true);
});

test('persistent buffering falls back and preserves position', () => {
  const app = setup();
  app.video.emit('waiting');
  app.video.emit('stalled');
  assert.equal(app.timers.size, 1);
  app.fireTimeouts();
  assert.equal(app.video.src, '/economy.mp4?v=def');
  app.video.emit('loadedmetadata');
  assert.equal(app.video.currentTime, 12);
  assert.equal(app.video.paused, false);
});

test('paused or sufficiently buffered video does not fall back', () => {
  const app = setup();
  app.video.paused = true;
  app.video.emit('waiting');
  assert.equal(app.timers.size, 0);
  app.video.paused = false;
  app.video.readyState = 4;
  app.video.emit('stalled');
  assert.equal(app.timers.size, 0);
});

test('manual quality choice disables automatic downgrade', () => {
  const app = setup();
  app.nodes.quality.value = 'hd';
  app.nodes.quality.emit('change');
  app.video.emit('loadedmetadata');
  assert.equal(app.nodes.automatic.checked, false);
  app.video.emit('waiting');
  app.fireTimeouts();
  assert.equal(app.video.src, '/watch.mp4?v=abc');
});

test('two sustained high-drop windows trigger economy mode', () => {
  const app = setup();
  app.video.emit('playing');
  app.sample();
  app.video.stats = {totalVideoFrames: 60, droppedVideoFrames: 10};
  app.sample();
  assert.equal(app.video.src, '/watch.mp4?v=abc');
  app.video.stats = {totalVideoFrames: 120, droppedVideoFrames: 20};
  app.sample();
  assert.equal(app.video.src, '/economy.mp4?v=def');
});

test('ordinary playback and hidden tabs do not cause downgrade', () => {
  const app = setup();
  app.video.emit('playing');
  app.sample();
  app.video.stats = {totalVideoFrames: 60, droppedVideoFrames: 1};
  app.sample();
  app.video.stats = {totalVideoFrames: 120, droppedVideoFrames: 2};
  app.sample();
  assert.equal(app.video.src, '/watch.mp4?v=abc');
  app.document.hidden = true;
  app.document.emit('visibilitychange');
  assert.equal(app.intervals.size, 0);
});
