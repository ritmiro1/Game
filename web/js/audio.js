/* РЕЗОНАНС — звук: полностью процедурный (WebAudio), без файлов */
(function () {
  'use strict';
  const A = { ctx: null, master: null, musicGain: null, on: true, music: true, started: false };
  let musicTimer = null;

  function ensure() {
    if (A.ctx) return A.ctx;
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return null;
    A.ctx = new Ctx();
    A.master = A.ctx.createGain();
    A.master.gain.value = 0.5;
    A.master.connect(A.ctx.destination);
    A.musicGain = A.ctx.createGain();
    A.musicGain.gain.value = 0.0;
    A.musicGain.connect(A.master);
    return A.ctx;
  }

  function now() { return A.ctx ? A.ctx.currentTime : 0; }

  function tone(freq, dur, type, vol, slideTo, dest) {
    const c = ensure(); if (!c || !A.on) return;
    const o = c.createOscillator(), g = c.createGain();
    o.type = type || 'sine';
    o.frequency.setValueAtTime(freq, now());
    if (slideTo) o.frequency.exponentialRampToValueAtTime(Math.max(20, slideTo), now() + dur);
    g.gain.setValueAtTime(0.0001, now());
    g.gain.exponentialRampToValueAtTime(vol || 0.15, now() + 0.008);
    g.gain.exponentialRampToValueAtTime(0.0001, now() + dur);
    o.connect(g); g.connect(dest || A.master);
    o.start(); o.stop(now() + dur + 0.02);
  }

  function noise(dur, vol, filterFreq, q) {
    const c = ensure(); if (!c || !A.on) return;
    const len = Math.max(1, Math.floor(c.sampleRate * dur));
    const buf = c.createBuffer(1, len, c.sampleRate);
    const d = buf.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * (1 - i / len);
    const src = c.createBufferSource(); src.buffer = buf;
    const f = c.createBiquadFilter(); f.type = 'lowpass';
    f.frequency.setValueAtTime(filterFreq || 1200, now());
    f.frequency.exponentialRampToValueAtTime(180, now() + dur);
    f.Q.value = q || 1;
    const g = c.createGain();
    g.gain.setValueAtTime(vol || 0.2, now());
    g.gain.exponentialRampToValueAtTime(0.0001, now() + dur);
    src.connect(f); f.connect(g); g.connect(A.master);
    src.start();
  }

  const SFX = {
    click() { tone(520, 0.05, 'square', 0.05, 700); },
    hover() { tone(880, 0.03, 'sine', 0.025); },
    select() { tone(660, 0.09, 'triangle', 0.1, 990); },
    move() { tone(300, 0.12, 'sine', 0.09, 210); noise(0.08, 0.05, 900); },
    attack() { noise(0.16, 0.28, 2600, 1.4); tone(150, 0.14, 'sawtooth', 0.12, 70); },
    hit() { noise(0.12, 0.22, 1800); tone(110, 0.1, 'square', 0.09, 60); },
    kill() { noise(0.34, 0.3, 900); tone(180, 0.3, 'sawtooth', 0.12, 40); },
    heal() { tone(520, 0.18, 'sine', 0.1, 880); tone(880, 0.22, 'sine', 0.06, 1320); },
    barrier() { tone(220, 0.22, 'triangle', 0.11, 440); tone(330, 0.28, 'sine', 0.06, 660); },
    impulse() { noise(0.3, 0.3, 4000, 0.8); tone(90, 0.35, 'sine', 0.2, 40); },
    harpoon() { tone(900, 0.14, 'sawtooth', 0.07, 240); noise(0.1, 0.12, 2200); },
    cleave() { noise(0.22, 0.3, 3000, 1.2); tone(240, 0.2, 'square', 0.1, 120); },
    summon() { tone(330, 0.3, 'triangle', 0.09, 660); tone(495, 0.35, 'sine', 0.06, 990); },
    turn() { tone(440, 0.1, 'sine', 0.08); setTimeout(() => tone(660, 0.14, 'sine', 0.07), 90); },
    energy() { tone(1200, 0.06, 'sine', 0.03, 1600); },
    win() { [523, 659, 784, 1046].forEach((f, i) => setTimeout(() => tone(f, 0.5, 'triangle', 0.12), i * 110)); },
    lose() { [392, 330, 262, 196].forEach((f, i) => setTimeout(() => tone(f, 0.6, 'sawtooth', 0.09), i * 140)); },
    draw() { [440, 440].forEach((f, i) => setTimeout(() => tone(f, 0.4, 'sine', 0.08), i * 220)); },
    error() { tone(150, 0.12, 'square', 0.08, 90); }
  };

  function play(name) {
    if (!A.on) return;
    const f = SFX[name];
    if (f) { try { f(); } catch (e) { } }
  }

  function startMusic() {
    const c = ensure(); if (!c) return;
    if (musicTimer) return;
    A.musicGain.gain.linearRampToValueAtTime(A.music ? 0.16 : 0, now() + 2);
    const scale = [110, 146.8, 164.8, 196, 220, 261.6];
    let step = 0;
    const pad = () => {
      if (!A.music) return;
      const f = scale[step % scale.length];
      const o = c.createOscillator(), g = c.createGain(), lp = c.createBiquadFilter();
      o.type = step % 3 === 0 ? 'triangle' : 'sine';
      o.frequency.value = f * (step % 4 === 3 ? 2 : 1);
      lp.type = 'lowpass'; lp.frequency.value = 900;
      g.gain.setValueAtTime(0.0001, now());
      g.gain.linearRampToValueAtTime(0.5, now() + 1.4);
      g.gain.linearRampToValueAtTime(0.0001, now() + 4.2);
      o.connect(lp); lp.connect(g); g.connect(A.musicGain);
      o.start(); o.stop(now() + 4.4);
      step++;
    };
    pad();
    musicTimer = setInterval(pad, 2600);
  }

  function setSound(v) { A.on = !!v; }
  function setMusic(v) {
    A.music = !!v;
    if (A.musicGain && A.ctx) A.musicGain.gain.linearRampToValueAtTime(A.music ? 0.16 : 0, now() + 0.6);
    if (A.music) startMusic();
  }
  function unlock() {
    ensure();
    if (A.ctx && A.ctx.state === 'suspended') A.ctx.resume();
    if (!A.started) { A.started = true; startMusic(); }
  }

  window.RZAudio = { play: play, setSound: setSound, setMusic: setMusic, unlock: unlock, state: A };
})();
