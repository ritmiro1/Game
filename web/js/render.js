/* РЕЗОНАНС — рендер арены, анимации, частицы, фон */
(function () {
  'use strict';
  const RR = {};
  const SIDE = ['#4fd8ff', '#ff6d9d'];
  const SIDE_DARK = ['#0b3a4c', '#4a1230'];

  let canvas = null, ctx = null, dpr = 1;
  let state = null;
  let view = { selected: -1, actions: [], hover: -1, targeting: null, showHints: true };
  let layout = null;
  let fx = [];
  let rafId = 0;
  let active = false;
  let shakeEnd = 0, shakeMag = 0;
  let lastClick = 0;
  let previewAction = null;
  let speedMul = 1;
  let onPick = null;

  /* ---------------- утилиты ---------------- */
  function hexPath(c, x, y, r, rot) {
    rot = rot || 0;
    c.beginPath();
    for (let i = 0; i < 6; i++) {
      const a = Math.PI / 180 * (60 * i - 90) + rot;
      const px = x + Math.cos(a) * r, py = y + Math.sin(a) * r;
      if (i === 0) c.moveTo(px, py); else c.lineTo(px, py);
    }
    c.closePath();
  }
  function lerp(a, b, t) { return a + (b - a) * t; }
  function ease(t) { return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t; }
  function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
  function rgba(hex, a) {
    const h = hex.replace('#', '');
    const r = parseInt(h.substr(0, 2), 16), g = parseInt(h.substr(2, 2), 16), b = parseInt(h.substr(4, 2), 16);
    return 'rgba(' + r + ',' + g + ',' + b + ',' + a + ')';
  }

  /* ---------------- разметка ---------------- */
  function computeLayout() {
    if (!state) return;
    const b = state.board;
    const host = canvas.parentElement;
    if (!host) return;
    const cw = Math.round(host.clientWidth), ch = Math.round(host.clientHeight);
    if (cw < 40 || ch < 40) return;
    const raw = [];
    let minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (let i = 0; i < b.N; i++) {
      const q = b.cells[i][0], r = b.cells[i][1];
      const x = Math.sqrt(3) * (q + r / 2), y = 1.5 * r;
      raw.push({ x: x, y: y });
      if (x < minX) minX = x; if (x > maxX) maxX = x;
      if (y < minY) minY = y; if (y > maxY) maxY = y;
    }
    const wu = (maxX - minX) + 2.2, hu = (maxY - minY) + 2.4;
    const pad = 10;
    const s = Math.max(8, Math.min((cw - pad) / wu, (ch - pad) / hu));
    const cx0 = (minX + maxX) / 2, cy0 = (minY + maxY) / 2;
    const cells = raw.map(function (p) { return { x: (p.x - cx0) * s, y: (p.y - cy0) * s }; });
    dpr = Math.min(2, window.devicePixelRatio || 1);
    canvas.width = Math.round(cw * dpr);
    canvas.height = Math.round(ch * dpr);
    canvas.style.width = cw + 'px';
    canvas.style.height = ch + 'px';
    layout = { s: s, cells: cells, cw: cw, ch: ch, ox: cw / 2, oy: ch / 2 };
  }

  RR.resize = function () { if (state) { computeLayout(); } };

  /* ---------------- анимации ---------------- */
  function now() { return performance.now(); }

  function addFx(o) { o.t0 = o.t0 === undefined ? now() : o.t0; fx.push(o); return o; }

  const DUR = { slide: 170, hit: 210, kill: 420, ring: 360, beam: 230, heal: 260, float: 620, summon: 320, barrier: 320, mark: 300, turn: 0 };

  RR.playEvents = function (events, opts) {
    opts = opts || {};
    const speed = (opts.speed || 1) * speedMul;
    return new Promise(function (resolve) {
      if (!events || !events.length) return resolve();
      let t = now();
      const at = layout && layout.cells ? layout.cells : null;
      events.forEach(function (ev) {
        let d = 0;
        switch (ev.e) {
          case 'move': {
            const from = at && at[ev.from], to = at && at[ev.to];
            if (from && to) {
              const dist = Math.hypot(to.x - from.x, to.y - from.y) / (layout.s * 2);
              d = DUR.slide * clamp(0.7 + dist * 0.25, 0.7, 1.6);
              addFx({ kind: 'slide', u: ev.u, from: ev.from, to: ev.to, dur: d, t0: t });
            }
            d = d || 90;
            RZAudio.play('move');
            break;
          }
          case 'attack':
            addFx({ kind: 'beam', from: ev.from, to: state.uCell[ev.target], dur: DUR.beam, t0: t, color: SIDE[1 - state.side] });
            RZAudio.play('attack');
            d = 120; break;
          case 'hit': {
            addFx({ kind: 'hit', cell: ev.cell, amt: ev.amt, dur: DUR.hit, t0: t });
            addFx({ kind: 'float', cell: ev.cell, text: '−' + ev.amt, color: '#ff9aa8', dur: DUR.float, t0: t });
            RZAudio.play('hit');
            shakeMag = Math.max(shakeMag, Math.min(9, ev.amt * 2.2)); shakeEnd = now() + 200;
            d = 110; break;
          }
          case 'retal': d = 0; break;
          case 'death':
            addFx({ kind: 'burst', cell: ev.cell, color: SIDE[ev.owner], dur: DUR.kill, t0: t });
            addFx({ kind: 'ghost', cell: ev.cell, type: ev.type, owner: ev.owner, dur: DUR.kill, t0: t });
            RZAudio.play('kill');
            shakeMag = Math.max(shakeMag, 12); shakeEnd = now() + 320;
            d = DUR.kill * 0.7; break;
          case 'heal':
            addFx({ kind: 'heal', cell: ev.cell, dur: DUR.heal, t0: t });
            RZAudio.play('heal');
            d = 90; break;
          case 'barrier':
            addFx({ kind: 'barrier', cell: ev.cell, dur: DUR.barrier, t0: t });
            RZAudio.play('barrier');
            d = 120; break;
          case 'impulse':
            addFx({ kind: 'ring', cell: ev.cell, color: '#9fe8ff', dur: 420, t0: t, r: layout ? layout.s * 2.6 : 100 });
            RZAudio.play('impulse');
            shakeMag = Math.max(shakeMag, 7); shakeEnd = now() + 240;
            d = 180; break;
          case 'cleave':
            addFx({ kind: 'ring', cell: ev.cell, color: '#ffd479', dur: 340, t0: t, r: layout ? layout.s * 1.9 : 80 });
            RZAudio.play('cleave');
            d = 140; break;
          case 'harpoon':
            addFx({ kind: 'beam', from: ev.from, to: ev.target !== undefined && state.uCell[ev.target] >= 0 ? state.uCell[ev.target] : ev.from, dur: DUR.beam, t0: t, color: '#ffd479' });
            RZAudio.play('harpoon');
            d = 110; break;
          case 'mark':
            addFx({ kind: 'mark', cell: ev.cell, dur: DUR.mark, t0: t });
            d = 120; break;
          case 'resonance':
            addFx({ kind: 'ring', cell: ev.cell, color: '#b6ff5f', dur: 400, t0: t, r: layout ? layout.s * 1.8 : 70 });
            d = 140; break;
          case 'push':
            d = 30; break;
          case 'summon':
            addFx({ kind: 'summon', cell: ev.cell, dur: DUR.summon, t0: t });
            RZAudio.play('summon');
            d = 220; break;
          case 'over': d = 0; break;
          case 'turn': RZAudio.play('turn'); d = 0; break;
          default: d = 0;
        }
        t += d / speed;
      });
      const total = t - now() + 60;
      setTimeout(resolve, Math.max(80, total));
    });
  };

  /* ---------------- частицы ---------------- */
  const parts = [];
  function spawnParts(cell, color, n, power) {
    if (!layout) return;
    const c = layout.cells[cell];
    if (!c) return;
    for (let i = 0; i < n; i++) {
      const a = Math.random() * Math.PI * 2, sp = (0.5 + Math.random()) * (power || 1.6);
      parts.push({
        x: c.x, y: c.y, vx: Math.cos(a) * sp, vy: Math.sin(a) * sp - 0.4,
        life: 0.5 + Math.random() * 0.6, age: 0, color: color, r: 1 + Math.random() * 2.4
      });
    }
  }

  /* ---------------- рисование ---------------- */
  function drawBackground(c) {
    const g = c.createRadialGradient(layout.ox, layout.oy, 20, layout.ox, layout.oy, Math.max(layout.cw, layout.ch) * 0.7);
    g.addColorStop(0, 'rgba(22,34,62,.55)');
    g.addColorStop(0.45, 'rgba(9,13,24,.5)');
    g.addColorStop(1, 'rgba(3,4,9,0)');
    c.fillStyle = g;
    c.fillRect(0, 0, layout.cw, layout.ch);
  }

  function drawCells(c, t) {
    const b = state.board, s = layout.s;
    for (let i = 0; i < b.N; i++) {
      const p = layout.cells[i];
      if (!p) continue;
      const blocked = b.blocked[i];
      hexPath(c, p.x, p.y, s * 0.94);
      if (blocked) {
        const gg = c.createLinearGradient(p.x - s, p.y - s, p.x + s, p.y + s);
        gg.addColorStop(0, '#131a29'); gg.addColorStop(1, '#05070d');
        c.fillStyle = gg; c.fill();
        c.strokeStyle = 'rgba(150,180,225,.28)'; c.lineWidth = 1.3; c.stroke();
        // штриховка камня
        c.save(); c.beginPath(); c.arc(p.x, p.y, s * 0.9, 0, 7); c.clip();
        c.strokeStyle = 'rgba(150,180,225,.14)'; c.lineWidth = 2.4;
        for (let k = -2; k <= 2; k++) {
          c.beginPath(); c.moveTo(p.x - s, p.y + k * s * 0.42); c.lineTo(p.x + s, p.y + k * s * 0.42 + s * 0.55); c.stroke();
        }
        c.strokeStyle = 'rgba(90,130,190,.35)'; c.lineWidth = 1.2;
        for (let k = -1; k <= 1; k++) {
          c.beginPath(); c.moveTo(p.x + k * s * 0.4 - s * 0.5, p.y - s * 0.7); c.lineTo(p.x + k * s * 0.4 + s * 0.2, p.y + s * 0.7); c.stroke();
        }
        c.restore();
      } else {
        const isNode = b.node[i];
        const isBase = (i === b.base[0] || i === b.base[1]);
        if (isNode) {
          const rg = c.createRadialGradient(p.x, p.y, s * 0.1, p.x, p.y, s);
          rg.addColorStop(0, 'rgba(96,62,150,.55)');
          rg.addColorStop(1, 'rgba(30,18,52,.55)');
          c.fillStyle = rg;
        } else {
          c.fillStyle = isBase ? 'rgba(26,44,72,.65)' : 'rgba(150,190,255,.045)';
        }
        c.fill();
        c.strokeStyle = isNode ? 'rgba(190,140,255,.4)' : isBase ? 'rgba(110,190,255,.3)' : 'rgba(120,180,255,.12)';
        c.lineWidth = 1.1;
        c.stroke();
        if (isNode) {
          const pulse = 0.5 + 0.5 * Math.sin(t / 620 + i);
          c.beginPath(); c.arc(p.x, p.y, s * (0.34 + pulse * 0.07), 0, 7);
          c.strokeStyle = 'rgba(210,165,255,' + (0.4 + pulse * 0.45) + ')'; c.lineWidth = 1.8; c.stroke();
          c.beginPath(); c.arc(p.x, p.y, s * 0.16, 0, 7);
          c.fillStyle = 'rgba(220,175,255,' + (0.3 + pulse * 0.4) + ')'; c.fill();
        }
        if (isBase) {
          const side = i === b.base[0] ? 0 : 1;
          c.save();
          c.globalAlpha = 0.5;
          hexPath(c, p.x, p.y, s * 0.72);
          c.strokeStyle = SIDE[side]; c.lineWidth = 1.4; c.setLineDash([4, 5]); c.stroke();
          c.restore();
        }
      }
      // барьер
      if (state.barrier[i] > 0) {
        const pulse = 0.6 + 0.4 * Math.sin(t / 300 + i);
        hexPath(c, p.x, p.y, s * 0.86);
        c.fillStyle = 'rgba(120,200,255,' + (0.06 + pulse * 0.05) + ')';
        c.fill();
        c.strokeStyle = 'rgba(160,225,255,' + (0.35 + pulse * 0.35) + ')';
        c.lineWidth = 2;
        c.setLineDash([7, 5]); c.lineDashOffset = -t / 40; c.stroke(); c.setLineDash([]);
      }
    }
  }

  function drawHighlights(c, t) {
    const b = state.board, s = layout.s;
    // выбранный юнит
    if (view.selected >= 0 && state.uAlive[view.selected]) {
      const p = layout.cells[state.uCell[view.selected]];
      if (p) {
        const pulse = 0.5 + 0.5 * Math.sin(t / 220);
        hexPath(c, p.x, p.y, s * (1.02 + pulse * 0.05));
        c.strokeStyle = rgba(SIDE[state.uOwner[view.selected]], 0.6 + pulse * 0.35);
        c.lineWidth = 2.4; c.stroke();
        hexPath(c, p.x, p.y, s * 1.14);
        c.strokeStyle = rgba(SIDE[state.uOwner[view.selected]], 0.15 + pulse * 0.2);
        c.lineWidth = 1.2; c.stroke();
      }
    }
    // доступные действия
    (view.actions || []).forEach(function (a) {
      let cell = -1, color = SIDE[0], kind = 'move';
      if (a.t === 'move') { cell = a.to; kind = 'move'; }
      else if (a.t === 'atk') { cell = state.uCell[a.target]; kind = 'atk'; color = '#ff6d7d'; }
      else if (a.t === 'ab' && a.k === 'barrier') { cell = a.to; kind = 'ab'; color = '#8fe3ff'; }
      else if (a.t === 'ab' && a.target !== undefined && a.target >= 0) { cell = state.uCell[a.target]; kind = 'ab'; color = '#ffd479'; }
      else if (a.t === 'ab') { cell = state.uCell[a.u]; kind = 'ab'; color = '#b6ff5f'; }
      else if (a.t === 'summon') { cell = a.to; kind = 'sum'; color = '#b6ff5f'; }
      const p = layout.cells[cell];
      if (!p) return;
      const pulse = 0.5 + 0.5 * Math.sin(t / 260);
      c.save();
      if (kind === 'atk') {
        hexPath(c, p.x, p.y, s * (0.92 + pulse * 0.06));
        c.strokeStyle = 'rgba(255,90,110,' + (0.55 + pulse * 0.4) + ')'; c.lineWidth = 2.6; c.stroke();
        // прицел
        c.strokeStyle = 'rgba(255,120,140,' + (0.3 + pulse * 0.3) + ')'; c.lineWidth = 1.4;
        c.beginPath(); c.arc(p.x, p.y, s * 0.5, 0, 7); c.stroke();
      } else if (kind === 'ab') {
        c.beginPath(); c.arc(p.x, p.y, s * (0.42 + pulse * 0.1), 0, 7);
        c.strokeStyle = rgba(color, 0.85); c.lineWidth = 2.2; c.stroke();
      } else {
        c.beginPath(); c.arc(p.x, p.y, s * (0.17 + pulse * 0.05), 0, 7);
        c.fillStyle = rgba(color, 0.75); c.fill();
        c.beginPath(); c.arc(p.x, p.y, s * 0.34, 0, 7);
        c.strokeStyle = rgba(color, 0.2 + pulse * 0.15); c.lineWidth = 1; c.stroke();
      }
      c.restore();
    });
    // наведение
    if (view.hover >= 0 && layout.cells[view.hover]) {
      const p = layout.cells[view.hover];
      hexPath(c, p.x, p.y, s * 0.94);
      c.strokeStyle = 'rgba(220,240,255,.45)'; c.lineWidth = 1.6; c.stroke();
    }
  }

  function unitMaxHp(u) {
    const side = state.uOwner[u], type = state.uType[u];
    return RZ.startHp(state, side, type);
  }

  function drawUnit(c, x, y, type, owner, hp, maxHp, opts) {
    opts = opts || {};
    const s = layout.s;
    const col = SIDE[owner], dark = SIDE_DARK[owner];
    const isCore = type === 0;
    const r = s * (isCore ? 0.78 : 0.62) * (opts.scale || 1);
    c.save();
    c.globalAlpha = opts.alpha === undefined ? 1 : opts.alpha;
    if (opts.glow) { c.shadowColor = col; c.shadowBlur = 18; }
    // тело
    const g = c.createLinearGradient(x - r, y - r, x + r, y + r);
    g.addColorStop(0, rgba(col, 0.85));
    g.addColorStop(1, rgba(dark, 0.95));
    hexPath(c, x, y, r);
    c.fillStyle = g; c.fill();
    c.strokeStyle = rgba(col, 0.95); c.lineWidth = isCore ? 2.4 : 1.8; c.stroke();
    c.shadowBlur = 0;
    // внутренний контур
    hexPath(c, x, y, r * 0.72);
    c.strokeStyle = rgba('#ffffff', 0.16); c.lineWidth = 1; c.stroke();
    if (isCore) {
      c.save();
      c.translate(x, y); c.rotate((performance.now() / 2400) % (Math.PI * 2));
      c.beginPath();
      for (let i = 0; i < 3; i++) {
        const a = i * Math.PI * 2 / 3;
        const px = Math.cos(a) * r * 0.5, py = Math.sin(a) * r * 0.5;
        if (i === 0) c.moveTo(px, py); else c.lineTo(px, py);
      }
      c.closePath(); c.strokeStyle = rgba('#ffffff', 0.55); c.lineWidth = 1.6; c.stroke();
      c.restore();
    }
    // глиф
    const glyph = RZ.TYPES[type].glyph;
    c.fillStyle = isCore ? '#eaffff' : 'rgba(255,255,255,.92)';
    c.font = '600 ' + Math.round(r * (isCore ? 0.95 : 0.8)) + 'px "Segoe UI",system-ui,sans-serif';
    c.textAlign = 'center'; c.textBaseline = 'middle';
    c.fillText(glyph, x, y + r * 0.02);
    // HP
    const pips = Math.max(0, Math.min(maxHp, hp));
    const pw = Math.max(3, r * 0.2), gap = 2.5;
    const tw = maxHp * pw + (maxHp - 1) * gap;
    let px0 = x - tw / 2;
    for (let i = 0; i < maxHp; i++) {
      c.fillStyle = i < pips ? (hp / maxHp < 0.4 ? '#ff8f6b' : rgba(col, 0.95)) : 'rgba(255,255,255,.14)';
      c.fillRect(px0 + i * (pw + gap), y + r * 1.02, pw, 2.6);
    }
    c.restore();
  }

  function drawUnits(c, t) {
    const s = layout.s;
    const sliding = {};
    fx.forEach(function (f) { if (f.kind === 'slide') sliding[f.u] = f; });
    for (let u = 0; u < RZ.MAXU; u++) {
      if (!state.uAlive[u]) continue;
      const cell = state.uCell[u];
      const sl = sliding[u];
      let x, y;
      if (sl) {
        const a = layout.cells[sl.from], bb = layout.cells[sl.to];
        if (a && bb) {
          const k = clamp((now() - sl.t0) / sl.dur, 0, 1);
          const e = ease(k);
          x = lerp(a.x, bb.x, e); y = lerp(a.y, bb.y, e) - Math.sin(k * Math.PI) * s * 0.26;
        }
      }
      const p = layout.cells[cell];
      if (!p) continue;
      if (x === undefined) { x = p.x; y = p.y; }
      const maxHp = unitMaxHp(u);
      drawUnit(c, x, y, state.uType[u], state.uOwner[u], state.uHp[u], maxHp, { glow: state.uType[u] === 0 });
      // метка
      if (state.uMark[u] > 0) {
        const pulse = 0.5 + 0.5 * Math.sin(t / 200);
        c.save();
        c.strokeStyle = 'rgba(255,212,121,' + (0.6 + pulse * 0.4) + ')';
        c.lineWidth = 1.8;
        c.beginPath(); c.arc(x, y, s * 0.86, 0, 7); c.stroke();
        c.setLineDash([3, 4]); c.lineDashOffset = t / 60;
        c.beginPath(); c.arc(x, y, s * 0.94, 0, 7); c.stroke();
        c.restore();
      }
      // кулдаун
      if (state.uCd[u] > 0) {
        c.save();
        c.fillStyle = 'rgba(255,212,121,.85)';
        c.font = '700 ' + Math.round(s * 0.26) + 'px "Segoe UI",sans-serif';
        c.textAlign = 'center'; c.textBaseline = 'middle';
        c.fillText(state.uCd[u], x + s * 0.52, y - s * 0.46);
        c.restore();
      }
    }
  }

  function drawFx(c, t) {
    const s = layout.s;
    for (let i = fx.length - 1; i >= 0; i--) {
      const f = fx[i];
      const k = clamp((now() - f.t0) / f.dur, 0, 1);
      if (k >= 1 && f.kind !== 'slide') { fx.splice(i, 1); continue; }
      const p = f.cell !== undefined ? layout.cells[f.cell] : null;
      if (f.kind === 'hit' || f.kind === 'ghost' || f.kind === 'barrier' || f.kind === 'mark' || f.kind === 'summon' || f.kind === 'heal' || f.kind === 'ring' || f.kind === 'burst') {
        if (!p) { fx.splice(i, 1); continue; }
      }
      switch (f.kind) {
        case 'slide': if (k >= 1) fx.splice(i, 1); break;
        case 'hit': {
          const a = (1 - k);
          c.save();
          c.globalAlpha = a * 0.85;
          c.beginPath(); c.arc(p.x, p.y, s * (0.5 + k * 0.55), 0, 7);
          c.fillStyle = 'rgba(255,120,140,' + (1 - k) * 0.45 + ')'; c.fill();
          hexPath(c, p.x, p.y, s * 0.95);
          c.strokeStyle = 'rgba(255,180,190,' + (1 - k) + ')'; c.lineWidth = 2.4; c.stroke();
          c.restore();
          break;
        }
        case 'burst': {
          if (f.spawned === undefined) { f.spawned = 1; spawnParts(f.cell, f.color, 22, 2.4); }
          break;
        }
        case 'ghost': {
          const a = 1 - k;
          drawUnit(c, p.x, p.y, f.type, f.owner, Math.max(0, Math.round((1 - k) * 3)), 3, { alpha: a * 0.7, scale: 1 + k * 0.35 });
          break;
        }
        case 'float': {
          c.save();
          c.globalAlpha = 1 - k;
          c.fillStyle = f.color;
          c.font = '800 ' + Math.round(s * 0.42) + 'px "Segoe UI",sans-serif';
          c.textAlign = 'center';
          c.fillText(f.text, p.x, p.y - s * (0.5 + k * 0.9));
          c.restore();
          break;
        }
        case 'beam': {
          const a = layout.cells[f.from], b2 = layout.cells[f.to];
          if (!a || !b2) break;
          c.save();
          c.globalAlpha = 1 - k;
          c.strokeStyle = f.color || '#fff';
          c.lineWidth = 3 * (1 - k * 0.6);
          c.shadowColor = f.color || '#fff'; c.shadowBlur = 12;
          c.beginPath(); c.moveTo(a.x, a.y); c.lineTo(lerp(a.x, b2.x, Math.min(1, k * 2.2)), lerp(a.y, b2.y, Math.min(1, k * 2.2))); c.stroke();
          c.restore();
          break;
        }
        case 'ring': {
          c.save();
          c.globalAlpha = (1 - k) * 0.9;
          c.strokeStyle = f.color || '#fff';
          c.lineWidth = 3 * (1 - k) + 0.6;
          c.beginPath(); c.arc(p.x, p.y, f.r * ease(k), 0, 7); c.stroke();
          c.restore();
          break;
        }
        case 'barrier': {
          c.save();
          c.globalAlpha = 1 - k * 0.3;
          hexPath(c, p.x, p.y, s * 0.86 * (0.4 + k * 0.6));
          c.strokeStyle = 'rgba(160,225,255,' + (1 - k * 0.5) + ')';
          c.lineWidth = 2.6; c.stroke();
          c.restore();
          break;
        }
        case 'mark': {
          c.save();
          c.globalAlpha = 1 - k * 0.4;
          c.strokeStyle = 'rgba(255,212,121,.95)'; c.lineWidth = 2;
          const rr = s * (1.3 - k * 0.45);
          c.beginPath(); c.arc(p.x, p.y, rr, 0, 7); c.stroke();
          c.restore();
          break;
        }
        case 'heal': {
          c.save();
          c.globalAlpha = 1 - k;
          for (let q = 0; q < 3; q++) {
            const ang = q * 2.1 + now() / 400;
            c.fillStyle = 'rgba(182,255,95,.9)';
            c.font = '700 ' + Math.round(s * 0.3) + 'px sans-serif';
            c.textAlign = 'center';
            c.fillText('+', p.x + Math.cos(ang) * s * 0.5, p.y + Math.sin(ang) * s * 0.5 - k * s * 0.5);
          }
          c.restore();
          break;
        }
        case 'summon': {
          c.save();
          c.globalAlpha = 1 - k;
          c.strokeStyle = 'rgba(182,255,95,.8)'; c.lineWidth = 1.6;
          for (let q = 0; q < 3; q++) {
            c.beginPath(); c.arc(p.x, p.y, s * (1.5 - k * 1.1) * (0.6 + q * 0.25), 0, 7); c.stroke();
          }
          c.restore();
          break;
        }
      }
    }
    // частицы
    const dt = 1 / 60;
    for (let i = parts.length - 1; i >= 0; i--) {
      const pt = parts[i];
      pt.age += dt;
      if (pt.age >= pt.life) { parts.splice(i, 1); continue; }
      pt.x += pt.vx; pt.y += pt.vy;
      pt.vy += 0.06; pt.vx *= 0.985; pt.vy *= 0.985;
      const a = 1 - pt.age / pt.life;
      c.globalAlpha = a;
      c.fillStyle = pt.color;
      c.beginPath(); c.arc(pt.x, pt.y, pt.r * a + 0.4, 0, 7); c.fill();
    }
    c.globalAlpha = 1;
  }

  function frame() {
    rafId = requestAnimationFrame(frame);
    if (!active || !state || !layout) return;
    if (!canvas || !canvas.parentElement) return;
    if (!layout || canvas.parentElement.clientWidth !== layout.cw || canvas.parentElement.clientHeight !== layout.ch) {
      computeLayout();
      if (!layout) return;
    }
    const c = ctx;
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.clearRect(0, 0, layout.cw, layout.ch);
    let ox = 0, oy = 0;
    if (now() < shakeEnd) {
      const k = (shakeEnd - now()) / 240;
      ox = (Math.random() - 0.5) * shakeMag * k;
      oy = (Math.random() - 0.5) * shakeMag * k;
    }
    c.save();
    c.translate(layout.ox + ox, layout.oy + oy);
    const t = now();
    drawCells(c, t);
    drawHighlights(c, t);
    drawUnits(c, t);
    drawFx(c, t);
    c.restore();
  }

  /* ---------------- API ---------------- */
  RR.init = function (cv) {
    canvas = cv;
    ctx = canvas.getContext('2d');
    canvas.addEventListener('click', function (e) {
      const cell = RR.pick(e);
      if (onPick) onPick(cell, e);
    });
    canvas.addEventListener('mousemove', function (e) {
      const cell = RR.pick(e);
      if (cell !== view.hover) { view.hover = cell; if (cell >= 0) RZAudio.play('hover'); }
    });
    canvas.addEventListener('mouseleave', function () { view.hover = -1; });
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); if (onPick) onPick(-1, e); });
    if (!rafId) frame();
    window.addEventListener('resize', function () { RR.resize(); });
  };

  RR.pick = function (e) {
    if (!layout) return -1;
    const r = canvas.getBoundingClientRect();
    const x = (e.clientX - r.left) - layout.ox;
    const y = (e.clientY - r.top) - layout.oy;
    let best = -1, bd = layout.s * 0.92;
    for (let i = 0; i < layout.cells.length; i++) {
      const p = layout.cells[i];
      const d = Math.hypot(p.x - x, p.y - y);
      if (d < bd) { bd = d; best = i; }
    }
    return best;
  };

  RR.setSpeed = function (v) { speedMul = Math.max(0.25, Math.min(60, v || 1)); };
  RR.setState = function (s, reset) {
    state = s;
    if (reset || !layout) { computeLayout(); }
    if (reset) { fx.length = 0; parts.length = 0; }
  };
  RR.setView = function (v) { Object.assign(view, v); };
  RR.getView = function () { return view; };
  RR.onPick = function (fn) { onPick = fn; RR._pickHandler = fn; };
  RR.setActive = function (v) { active = !!v; if (v) computeLayout(); };
  RR.isActive = function () { return active; };
  RR.shake = function (m) { shakeMag = m; shakeEnd = now() + 300; };
  RR.layout = function () { return layout; };

  /* ---------------- живой фон ---------------- */
  RR.startBackground = function () {
    const cv = document.getElementById('bg');
    if (!cv) return;
    const c = cv.getContext('2d');
    let W = 0, H = 0, dprb = 1;
    const stars = [], hexes = [];
    function resize() {
      dprb = Math.min(2, window.devicePixelRatio || 1);
      W = window.innerWidth; H = window.innerHeight;
      cv.width = W * dprb; cv.height = H * dprb;
      cv.style.width = W + 'px'; cv.style.height = H + 'px';
      stars.length = 0;
      const n = Math.round(W * H / 14000);
      for (let i = 0; i < n; i++) stars.push({ x: Math.random() * W, y: Math.random() * H, r: Math.random() * 1.5 + 0.3, a: Math.random(), sp: 0.1 + Math.random() * 0.5 });
      hexes.length = 0;
      for (let i = 0; i < 9; i++) hexes.push({ x: Math.random() * W, y: Math.random() * H, r: 40 + Math.random() * 130, sp: (Math.random() - 0.5) * 0.12, rot: Math.random() * 6, rs: (Math.random() - 0.5) * 0.002, a: 0.03 + Math.random() * 0.05 });
    }
    resize();
    window.addEventListener('resize', resize);
    let tt = 0;
    function loop() {
      requestAnimationFrame(loop);
      tt += 1;
      c.setTransform(dprb, 0, 0, dprb, 0, 0);
      c.clearRect(0, 0, W, H);
      for (const s of stars) {
        s.a += 0.01 * s.sp;
        s.y += s.sp * 0.12;
        if (s.y > H) s.y = 0;
        c.globalAlpha = 0.25 + Math.abs(Math.sin(s.a)) * 0.55;
        c.fillStyle = '#bfe4ff';
        c.beginPath(); c.arc(s.x, s.y, s.r, 0, 7); c.fill();
      }
      for (const h of hexes) {
        h.x += h.sp; h.rot += h.rs;
        if (h.x > W + h.r) h.x = -h.r; if (h.x < -h.r) h.x = W + h.r;
        c.globalAlpha = h.a;
        c.strokeStyle = '#7fd8ff'; c.lineWidth = 1.4;
        c.save(); c.translate(h.x, h.y); c.rotate(h.rot);
        c.beginPath();
        for (let i = 0; i < 6; i++) {
          const a = i * Math.PI / 3;
          const px = Math.cos(a) * h.r, py = Math.sin(a) * h.r;
          if (i === 0) c.moveTo(px, py); else c.lineTo(px, py);
        }
        c.closePath(); c.stroke(); c.restore();
      }
      c.globalAlpha = 1;
    }
    loop();
  };

  window.RZRender = RR;
})();
