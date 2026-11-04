/* РЕЗОНАНС — движок правил. Детерминированный, без случайности в бою.
   Используется и в браузере, и на сервере (общий модуль). */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory(require('./content.js'));
  else { root.RZ = root.RZ || {}; Object.assign(root.RZ, factory(root.RZ)); }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (C) {
  'use strict';

  const DIRS = [[1, 0], [1, -1], [0, -1], [-1, 0], [-1, 1], [0, 1]];
  const MAXU = 40;
  const T = C.T;
  const MAX_ENERGY = 10;
  const TURN_LIMIT = 40;
  const ACTION_POINTS = 2;
  const UNIT_CAP = 8;
  const BARRIER_LIFE = 3;
  const MARK_LIFE = 4;
  const SUDDEN_DEATH = 24;
  const SUMMON_COST = 4;

  /* ---------- доска ---------- */
  const boardCache = {};
  function getBoard(mapId) {
    if (boardCache[mapId]) return boardCache[mapId];
    const map = C.MAP_BY_ID[mapId] || C.MAPS[0];
    const R = map.radius || 4;
    const cells = [], idx = {};
    for (let q = -R; q <= R; q++) for (let r = -R; r <= R; r++) {
      if (Math.abs(q + r) <= R) { idx[q + ',' + r] = cells.length; cells.push([q, r]); }
    }
    const N = cells.length;
    const nb = new Int16Array(N * 6).fill(-1);
    for (let i = 0; i < N; i++) {
      const q = cells[i][0], r = cells[i][1];
      for (let d = 0; d < 6; d++) {
        const k = (q + DIRS[d][0]) + ',' + (r + DIRS[d][1]);
        if (idx[k] !== undefined) nb[i * 6 + d] = idx[k];
      }
    }
    const blocked = new Uint8Array(N), node = new Uint8Array(N);
    (map.obstacles || []).forEach(function (p) { const i = idx[p[0] + ',' + p[1]]; if (i !== undefined) blocked[i] = 1; });
    (map.nodes || []).forEach(function (p) { const i = idx[p[0] + ',' + p[1]]; if (i !== undefined) node[i] = 1; });
    const base = new Int16Array(2);
    base[0] = idx[map.bases[0][0] + ',' + map.bases[0][1]];
    base[1] = idx[map.bases[1][0] + ',' + map.bases[1][1]];
    const nodeCells = [];
    for (let i = 0; i < N; i++) if (node[i]) nodeCells.push(i);
    const b = { map: map, R: R, N: N, cells: cells, idx: idx, nb: nb, blocked: blocked, node: node, nodeCells: nodeCells, base: base };
    boardCache[mapId] = b;
    return b;
  }

  function dist(b, a, c) {
    const q1 = b.cells[a][0], r1 = b.cells[a][1], q2 = b.cells[c][0], r2 = b.cells[c][1];
    const dq = q1 - q2, dr = r1 - r2;
    return Math.max(Math.abs(dq), Math.abs(dr), Math.abs(dq + dr));
  }
  function dirOf(b, from, to) {
    const q1 = b.cells[from][0], r1 = b.cells[from][1], q2 = b.cells[to][0], r2 = b.cells[to][1];
    const dq = q2 - q1, dr = r2 - r1;
    for (let d = 0; d < 6; d++) {
      const kq = DIRS[d][0], kr = DIRS[d][1];
      if (dq * kr - dr * kq === 0 && (dq * kq + dr * kr) > 0) return d;
    }
    return -1;
  }
  function step(b, cell, d, n) {
    let cur = cell;
    for (let i = 0; i < (n || 1); i++) {
      const nx = b.nb[cur * 6 + d];
      if (nx < 0) return -1;
      cur = nx;
    }
    return cur;
  }

  /* ---------- состояние ---------- */
  function factionMods(s, side) { return C.FACTIONS[s.faction[side]].mods || {}; }

  function startHp(s, side, type) {
    const t = C.TYPES[type];
    const m = factionMods(s, side);
    let hp = t.hp;
    if (type === T.CORE) hp += (m.coreHp || 0);
    else if (type === T.BLADE) hp += (m.bladeHp || 0);
    else if (type === T.WARD) hp += (m.wardHp || 0);
    else if (type === T.HOOK) hp += (m.hookHp || 0);
    return Math.max(1, hp);
  }
  function atkOf(s, i) {
    const type = s.uType[i];
    let a = C.TYPES[type].atk;
    if (type === T.BLADE) a += (factionMods(s, s.uOwner[i]).bladeAtk || 0);
    return a;
  }
  function leapOf(s, i) {
    const base = C.TYPES[s.uType[i]].move;
    const m = factionMods(s, s.uOwner[i]).runnerMove;
    return m ? m : base;
  }
  function harpoonRange(s, i) {
    return 2 + (factionMods(s, s.uOwner[i]).hookRange || 0);
  }

  function createState(opts) {
    opts = opts || {};
    const mapId = opts.mapId || 'core';
    const b = getBoard(mapId);
    const s = {
      board: b, mapId: mapId, radius: b.R,
      occ: new Int16Array(b.N),
      barrier: new Int8Array(b.N),
      uOwner: new Int8Array(MAXU).fill(-1),
      uType: new Int8Array(MAXU),
      uHp: new Int8Array(MAXU),
      uCell: new Int16Array(MAXU).fill(-1),
      uAlive: new Uint8Array(MAXU),
      uMark: new Int8Array(MAXU),
      uCd: new Int8Array(MAXU),
      energy: new Int16Array(2),
      od: ACTION_POINTS,
      side: 0,
      turn: 1,
      nextUnit: 0,
      faction: new Int8Array([opts.factions ? opts.factions[0] : 0, opts.factions ? opts.factions[1] : 1]),
      over: 0, winner: -1, actionCount: 0, ply: 0
    };
    placeArmy(s, 0);
    placeArmy(s, 1);
    for (let i = 0; i < 2; i++) s.energy[i] = 4 + (factionMods(s, i).startEnergy || 0);
    s.od = ACTION_POINTS;
    return s;
  }

  function spotCandidates(s, side) {
    const b = s.board, base = b.base[side], enemy = b.base[1 - side];
    const list = [];
    for (let i = 0; i < b.N; i++) {
      if (b.blocked[i] || i === base || i === enemy) continue;
      const dOwn = dist(b, base, i), dEnemy = dist(b, enemy, i);
      if (dOwn < 1 || dOwn > 3 || dOwn >= dEnemy) continue;
      list.push(i);
    }
    return list;
  }

  function placeArmy(s, side) {
    const b = s.board, base = b.base[side];
    spawn(s, side, T.CORE, base);
    const order = [T.BLADE, T.BLADE, T.WARD, T.HOOK, T.RES, T.RUNNER];
    const cand = spotCandidates(s, side);
    const placed = [base];
    for (let k = 0; k < order.length; k++) {
      const type = order[k];
      let bestCell = -1, bestScore = -1e9;
      for (let ci = 0; ci < cand.length; ci++) {
        const c = cand[ci];
        if (s.occ[c] !== 0) continue;
        let minD = 9;
        for (let p = 0; p < placed.length; p++) { const d = dist(b, placed[p], c); if (d < minD) minD = d; }
        if (minD < 2) continue;
        const dOwn = dist(b, base, c);
        let sc = Math.min(minD, 3) * 10 - Math.abs(dOwn - 2.5) * 3;
        if (b.node[c]) sc += 4;
        if (sc > bestScore) { bestScore = sc; bestCell = c; }
      }
      if (bestCell < 0) { // фолбэк: первая свободная
        for (let ci = 0; ci < cand.length; ci++) if (s.occ[cand[ci]] === 0) { bestCell = cand[ci]; break; }
      }
      if (bestCell >= 0) { spawn(s, side, type, bestCell); placed.push(bestCell); }
    }
  }

  function spawn(s, side, type, cell, hpOverride) {
    const i = s.nextUnit++;
    if (i >= MAXU) return -1;
    s.uOwner[i] = side; s.uType[i] = type;
    s.uHp[i] = hpOverride || startHp(s, side, type);
    s.uCell[i] = cell; s.uAlive[i] = 1; s.uMark[i] = 0; s.uCd[i] = 0;
    if (cell >= 0) s.occ[cell] = i + 1;
    return i;
  }

  function clone(s) {
    const n = Object.create(stateProto);
    n.board = s.board; n.mapId = s.mapId; n.radius = s.radius;
    n.occ = s.occ.slice(); n.barrier = s.barrier.slice();
    n.uOwner = s.uOwner.slice(); n.uType = s.uType.slice(); n.uHp = s.uHp.slice();
    n.uCell = s.uCell.slice(); n.uAlive = s.uAlive.slice(); n.uMark = s.uMark.slice(); n.uCd = s.uCd.slice();
    n.energy = s.energy.slice(); n.faction = s.faction.slice();
    n.od = s.od; n.side = s.side; n.turn = s.turn; n.nextUnit = s.nextUnit;
    n.over = s.over; n.winner = s.winner; n.actionCount = s.actionCount; n.ply = s.ply;
    return n;
  }
  const stateProto = {};

  function copyState(d, s) {
    d.occ.set(s.occ); d.barrier.set(s.barrier);
    d.uOwner.set(s.uOwner); d.uType.set(s.uType); d.uHp.set(s.uHp); d.uCell.set(s.uCell);
    d.uAlive.set(s.uAlive); d.uMark.set(s.uMark); d.uCd.set(s.uCd);
    d.energy.set(s.energy); d.faction.set(s.faction);
    d.board = s.board; d.mapId = s.mapId; d.radius = s.radius;
    d.od = s.od; d.side = s.side; d.turn = s.turn; d.nextUnit = s.nextUnit;
    d.over = s.over; d.winner = s.winner; d.actionCount = s.actionCount; d.ply = s.ply;
    return d;
  }

  /* ---------- запросы ---------- */
  function unitCount(s, side) {
    let n = 0;
    for (let i = 0; i < MAXU; i++) if (s.uAlive[i] && s.uOwner[i] === side) n++;
    return n;
  }
  function nodeControl(s, side) {
    let n = 0;
    const nc = s.board.nodeCells;
    for (let k = 0; k < nc.length; k++) {
      const o = s.occ[nc[k]];
      if (o > 0 && s.uOwner[o - 1] === side) n++;
    }
    return n;
  }
  function income(s, side) {
    let inc = 1 + nodeControl(s, side);
    for (let i = 0; i < MAXU; i++) {
      if (s.uAlive[i] && s.uOwner[i] === side && s.uType[i] === T.RES)
        inc += 1 + (factionMods(s, side).resIncome || 0);
    }
    return inc;
  }
  function canStand(s, c) {
    const b = s.board;
    return c >= 0 && c < b.N && !b.blocked[c] && s.occ[c] === 0 && s.barrier[c] <= 0;
  }
  function score(s, side) {
    let v = 0;
    for (let i = 0; i < MAXU; i++) {
      if (!s.uAlive[i] || s.uOwner[i] !== side) continue;
      v += (s.uType[i] === T.CORE ? 10 : 1) * s.uHp[i];
    }
    return v + nodeControl(s, side) * 2 + s.energy[side] * 0.1;
  }

  /* ---------- генерация действий ---------- */
  function getMoves(s, side) {
    const out = [];
    if (s.over) return out;
    const me = side === undefined ? s.side : side;
    if (me !== s.side) return out;
    const b = s.board, energy = s.energy[me];
    for (let i = 0; i < MAXU; i++) {
      if (!s.uAlive[i] || s.uOwner[i] !== me) continue;
      const type = s.uType[i], t = C.TYPES[type], cell = s.uCell[i];
      // движение
      if (t.move > 0) {
        if (type === T.RUNNER) {
          const L = leapOf(s, i);
          for (let c = 0; c < b.N; c++) if (dist(b, cell, c) === L && canStand(s, c)) out.push({ t: 'move', u: i, to: c });
        } else {
          for (let d = 0; d < 6; d++) {
            const c = b.nb[cell * 6 + d];
            if (c >= 0 && canStand(s, c)) out.push({ t: 'move', u: i, to: c });
          }
        }
      }
      // атака
      if (t.atk > 0 && energy >= t.atkCost) {
        for (let d = 0; d < 6; d++) {
          const c = b.nb[cell * 6 + d];
          if (c < 0) continue;
          const o = s.occ[c];
          if (o > 0 && s.uOwner[o - 1] !== me) out.push({ t: 'atk', u: i, target: o - 1 });
        }
      }
      // способности
      const ab = t.ability;
      if (ab && s.uCd[i] <= 0 && energy >= ab.cost) {
        switch (ab.id) {
          case 'impulse': out.push({ t: 'ab', u: i, k: 'impulse' }); break;
          case 'cleave': {
            let hit = false;
            for (let d = 0; d < 6; d++) { const c = b.nb[cell * 6 + d]; if (c >= 0 && s.occ[c] > 0 && s.uOwner[s.occ[c] - 1] !== me) { hit = true; break; } }
            if (hit) out.push({ t: 'ab', u: i, k: 'cleave' });
            break;
          }
          case 'barrier':
            for (let d = 0; d < 6; d++) {
              const c = b.nb[cell * 6 + d];
              if (c >= 0 && !b.blocked[c] && s.occ[c] === 0 && s.barrier[c] <= 0) out.push({ t: 'ab', u: i, k: 'barrier', to: c });
            }
            break;
          case 'mark':
            for (let j = 0; j < MAXU; j++) {
              if (!s.uAlive[j] || s.uOwner[j] === me) continue;
              if (s.uType[j] === T.CORE) continue;
              if (dist(b, cell, s.uCell[j]) <= 2) out.push({ t: 'ab', u: i, k: 'mark', target: j });
            }
            break;
          case 'harpoon': {
            const R2 = harpoonRange(s, i);
            for (let j = 0; j < MAXU; j++) {
              if (!s.uAlive[j] || s.uOwner[j] === me) continue;
              const d = dist(b, cell, s.uCell[j]);
              if (d >= 2 && d <= R2) out.push({ t: 'ab', u: i, k: 'harpoon', target: j });
            }
            break;
          }
          case 'resonance': {
            let need = s.uHp[i] < startHp(s, me, T.RES);
            if (!need) for (let d = 0; d < 6; d++) {
              const c = b.nb[cell * 6 + d]; if (c < 0) continue; const o = s.occ[c];
              if (o > 0 && s.uOwner[o - 1] === me && s.uHp[o - 1] < startHp(s, me, s.uType[o - 1])) { need = true; break; }
            }
            if (need) out.push({ t: 'ab', u: i, k: 'resonance' });
            break;
          }
        }
      }
    }
    // призыв
    if (energy >= SUMMON_COST && unitCount(s, me) < UNIT_CAP) {
      const base = b.base[me];
      for (let d = 0; d < 6; d++) {
        const c = b.nb[base * 6 + d];
        if (c >= 0 && canStand(s, c)) out.push({ t: 'summon', to: c });
      }
    }
    out.push({ t: 'end' });
    return out;
  }

  function sameAction(a, b) {
    if (!a || !b || a.t !== b.t) return false;
    if (a.t === 'move') return a.u === b.u && a.to === b.to;
    if (a.t === 'atk') return a.u === b.u && a.target === b.target;
    if (a.t === 'ab') return a.u === b.u && a.k === b.k && a.to === b.to && a.target === b.target;
    if (a.t === 'summon') return a.to === b.to;
    return true;
  }
  function isLegal(s, a) {
    const mv = getMoves(s);
    for (let i = 0; i < mv.length; i++) if (sameAction(mv[i], a)) return true;
    return false;
  }

  /* ---------- применение действий ---------- */
  function kill(s, i, ev) {
    if (!s.uAlive[i]) return;
    s.uAlive[i] = 0;
    if (s.uCell[i] >= 0) s.occ[s.uCell[i]] = 0;
    ev.push({ e: 'death', u: i, owner: s.uOwner[i], type: s.uType[i], cell: s.uCell[i] });
    const killerSide = 1 - s.uOwner[i];
    const m = factionMods(s, killerSide);
    if (m.killEnergy) s.energy[killerSide] = Math.min(MAX_ENERGY, s.energy[killerSide] + m.killEnergy);
    if (s.uType[i] === T.CORE) { s.over = 1; s.winner = killerSide; }
  }

  function damage(s, target, amt, ev) {
    if (!s.uAlive[target]) return 0;
    if (s.uMark[target] > 0) amt += 1;
    if (s.turn >= SUDDEN_DEATH) amt += 1;
    if (s.uType[target] === T.CORE) amt *= 2;
    s.uHp[target] -= amt;
    ev.push({ e: 'hit', u: target, cell: s.uCell[target], amt: amt, hp: Math.max(0, s.uHp[target]), owner: s.uOwner[target] });
    if (s.uHp[target] <= 0) kill(s, target, ev);
    return amt;
  }

  function moveUnit(s, i, to, ev) {
    const from = s.uCell[i];
    s.occ[from] = 0; s.occ[to] = i + 1; s.uCell[i] = to;
    ev.push({ e: 'move', u: i, from: from, to: to, owner: s.uOwner[i] });
  }

  function applyInPlace(s, a, evOut) {
    const ev = evOut || [];
    if (s.over) return ev;
    const b = s.board, me = s.side;
    if (a.t === 'end') { endTurn(s, ev); return ev; }
    s.od--;
    if (a.t === 'move') {
      if (canStand(s, a.to) && s.uAlive[a.u] && s.uOwner[a.u] === me) moveUnit(s, a.u, a.to, ev);
    } else if (a.t === 'atk') {
      const u = a.u, tg = a.target;
      if (s.uAlive[u] && s.uAlive[tg] && s.uOwner[u] === me && s.uOwner[tg] !== me) {
        const cost = C.TYPES[s.uType[u]].atkCost;
        s.energy[me] = Math.max(0, s.energy[me] - cost);
        ev.push({ e: 'attack', u: u, target: tg, from: s.uCell[u] });
        const dmg = atkOf(s, u);
        damage(s, tg, dmg, ev);
        if (false && s.uAlive[tg] && atkOf(s, tg) > 0 && s.uAlive[u]) {
          damage(s, u, 1, ev);
          ev.push({ e: 'retal', u: u, target: tg });
        }
      }
    } else if (a.t === 'ab') {
      const u = a.u;
      if (s.uAlive[u] && s.uOwner[u] === me) {
        const t = C.TYPES[s.uType[u]], ab = t.ability;
        s.energy[me] = Math.max(0, s.energy[me] - ab.cost);
        s.uCd[u] = ab.cd;
        const cell = s.uCell[u];
        if (ab.id === 'impulse') {
          ev.push({ e: 'impulse', u: u, cell: cell });
          for (let d = 0; d < 6; d++) {
            const c = b.nb[cell * 6 + d];
            if (c < 0) continue;
            const o = s.occ[c];
            if (o > 0 && s.uOwner[o - 1] !== me) {
              const nx = b.nb[c * 6 + d];
              if (nx >= 0 && canStand(s, nx)) { moveUnit(s, o - 1, nx, ev); ev.push({ e: 'push', c: nx }); }
              else { damage(s, o - 1, 1, ev); }
            }
          }
        } else if (ab.id === 'cleave') {
          ev.push({ e: 'cleave', u: u, cell: cell });
          for (let d = 0; d < 6; d++) {
            const c = b.nb[cell * 6 + d];
            if (c < 0) continue;
            const o = s.occ[c];
            if (o > 0 && s.uOwner[o - 1] !== me) damage(s, o - 1, 1, ev);
          }
        } else if (ab.id === 'barrier') {
          if (a.to >= 0 && !b.blocked[a.to] && s.occ[a.to] === 0) {
            s.barrier[a.to] = BARRIER_LIFE;
            ev.push({ e: 'barrier', cell: a.to });
          }
        } else if (ab.id === 'mark') {
          if (s.uAlive[a.target]) {
            s.uMark[a.target] = MARK_LIFE;
            ev.push({ e: 'mark', u: a.target, cell: s.uCell[a.target] });
          }
        } else if (ab.id === 'harpoon') {
          const tg = a.target;
          if (s.uAlive[tg] && s.uOwner[tg] !== me) {
            const d = dirOf(b, cell, s.uCell[tg]);
            const len = dist(b, cell, s.uCell[tg]);
            ev.push({ e: 'harpoon', u: u, target: tg, from: s.uCell[tg] });
            let dest = -1;
            if (d >= 0) for (let k = len - 1; k >= 1; k--) {
              const c = step(b, cell, d, k);
              if (c >= 0 && canStand(s, c)) { dest = c; break; }
            }
            if (dest >= 0) moveUnit(s, tg, dest, ev);
            else damage(s, tg, 1, ev);
          }
        } else if (ab.id === 'resonance') {
          ev.push({ e: 'resonance', u: u, cell: cell });
          const maxS = startHp(s, me, T.RES);
          if (s.uHp[u] < maxS) { s.uHp[u]++; ev.push({ e: 'heal', u: u, cell: cell }); }
          for (let d = 0; d < 6; d++) {
            const c = b.nb[cell * 6 + d];
            if (c < 0) continue;
            const o = s.occ[c];
            if (o > 0 && s.uOwner[o - 1] === me) {
              const oi = o - 1, mx = startHp(s, me, s.uType[oi]);
              if (s.uHp[oi] < mx) { s.uHp[oi]++; ev.push({ e: 'heal', u: oi, cell: c }); }
            }
          }
        }
      }
    } else if (a.t === 'summon') {
      if (s.energy[me] >= SUMMON_COST && canStand(s, a.to) && unitCount(s, me) < UNIT_CAP) {
        s.energy[me] -= SUMMON_COST;
        const ni = spawn(s, me, T.RUNNER, a.to);
        ev.push({ e: 'summon', u: ni, cell: a.to, owner: me });
        if (s.uCd[ni] !== undefined) s.uCd[ni] = 0;
      }
    }
    s.actionCount++; s.ply++;
    if (!s.over && s.od <= 0) endTurn(s, ev);
    return ev;
  }

  function endTurn(s, ev) {
    ev = ev || [];
    const me = s.side;
    for (let i = 0; i < MAXU; i++) {
      if (!s.uAlive[i]) continue;
      if (s.uOwner[i] === me) {
        if (s.uCd[i] > 0) s.uCd[i]--;
        if (s.uMark[i] > 0) s.uMark[i]--;
      }
    }
    for (let c = 0; c < s.board.N; c++) if (s.barrier[c] > 0) s.barrier[c]--;
    s.side = 1 - s.side;
    s.od = ACTION_POINTS;
    if (s.side === 0) s.turn++;
    s.energy[s.side] = Math.min(MAX_ENERGY, s.energy[s.side] + income(s, s.side));
    ev.push({ e: 'turn', side: s.side, turn: s.turn, income: income(s, s.side) });
    checkOver(s, ev);
  }

  function checkOver(s, ev) {
    if (s.over) return;
    if (unitCount(s, 0) === 0) { s.over = 1; s.winner = 1; }
    else if (unitCount(s, 1) === 0) { s.over = 1; s.winner = 0; }
    else if (s.turn > TURN_LIMIT) {
      const a = score(s, 0), b = score(s, 1);
      s.over = 1;
      s.winner = Math.abs(a - b) < 0.001 ? -1 : (a > b ? 0 : 1);
    }
    if (s.over) { s.winner = s.winner; if (ev) ev.push({ e: 'over', winner: s.winner }); }
  }

  function apply(s, a) {
    const n = clone(s);
    const ev = applyInPlace(n, a);
    return { state: n, events: ev };
  }

  /* ---------- сериализация ---------- */
  function encode(s) {
    const units = [];
    for (let i = 0; i < MAXU; i++) {
      if (!s.uAlive[i]) continue;
      units.push([i, s.uOwner[i], s.uType[i], s.uHp[i], s.uCell[i], s.uMark[i], s.uCd[i]]);
    }
    const barriers = [];
    for (let c = 0; c < s.board.N; c++) if (s.barrier[c] > 0) barriers.push([c, s.barrier[c]]);
    return {
      m: s.mapId, f: [s.faction[0], s.faction[1]], u: units, br: barriers,
      e: [s.energy[0], s.energy[1]], od: s.od, sd: s.side, tn: s.turn,
      nu: s.nextUnit, ov: s.over, wn: s.winner, ac: s.actionCount
    };
  }
  function decode(o) {
    const s = createState({ mapId: o.m, factions: o.f });
    s.occ.fill(0); s.barrier.fill(0);
    s.uAlive.fill(0); s.uOwner.fill(-1); s.uHp.fill(0); s.uCell.fill(-1); s.uMark.fill(0); s.uCd.fill(0);
    s.nextUnit = 0;
    (o.u || []).forEach(function (x) {
      const i = x[0];
      s.uOwner[i] = x[1]; s.uType[i] = x[2]; s.uHp[i] = x[3]; s.uCell[i] = x[4];
      s.uMark[i] = x[5]; s.uCd[i] = x[6]; s.uAlive[i] = 1;
      s.occ[x[4]] = i + 1;
      if (i + 1 > s.nextUnit) s.nextUnit = i + 1;
    });
    (o.br || []).forEach(function (x) { s.barrier[x[0]] = x[1]; });
    s.energy[0] = o.e[0]; s.energy[1] = o.e[1];
    s.od = o.od; s.side = o.sd; s.turn = o.tn; s.over = o.ov; s.winner = o.wn;
    s.actionCount = o.ac || 0;
    if (o.nu !== undefined) s.nextUnit = o.nu;
    return s;
  }

  /* ---------- пресеты ---------- */
  function createPuzzleState(p) {
    const s = createState({ mapId: p.map, factions: [0, 1] });
    s.occ.fill(0); s.uAlive.fill(0); s.uOwner.fill(-1); s.uHp.fill(0); s.uCell.fill(-1); s.uMark.fill(0); s.uCd.fill(0); s.barrier.fill(0);
    s.nextUnit = 0;
    p.pieces.forEach(function (pc) {
      const cell = s.board.idx[pc.cell[0] + ',' + pc.cell[1]];
      if (cell === undefined) return;
      const i = spawn(s, pc.side, pc.type, cell, pc.hp);
      if (pc.type === T.CORE && pc.side === 1 && p.oppCoreHp) s.uHp[i] = p.oppCoreHp;
      if (pc.type === T.CORE && pc.side === 0 && p.myCoreHp) s.uHp[i] = p.myCoreHp;
    });
    s.energy[0] = p.energy !== undefined ? p.energy : 6;
    s.energy[1] = p.oppEnergy !== undefined ? p.oppEnergy : 6;
    s.side = 0; s.od = ACTION_POINTS; s.turn = 1; s.over = 0; s.winner = -1;
    return s;
  }

  return {
    DIRS: DIRS, MAXU: MAXU, MAX_ENERGY: MAX_ENERGY, TURN_LIMIT: TURN_LIMIT,
    ACTION_POINTS: ACTION_POINTS, UNIT_CAP: UNIT_CAP, BARRIER_LIFE: BARRIER_LIFE, MARK_LIFE: MARK_LIFE, SUDDEN_DEATH: SUDDEN_DEATH, SUMMON_COST: SUMMON_COST,
    getBoard: getBoard, createState: createState, createPuzzleState: createPuzzleState,
    clone: clone, copyState: copyState, getMoves: getMoves, apply: apply, applyInPlace: applyInPlace,
    isLegal: isLegal, sameAction: sameAction, encode: encode, decode: decode,
    dist: dist, dirOf: dirOf, step: step, canStand: canStand,
    unitCount: unitCount, nodeControl: nodeControl, income: income, score: score,
    startHp: startHp, atkOf: atkOf, leapOf: leapOf, harpoonRange: harpoonRange,
    factionMods: factionMods, endTurn: endTurn, checkOver: checkOver, spawn: spawn, kill: kill
  };
});
