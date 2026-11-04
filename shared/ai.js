/* РЕЗОНАНС — ИИ: негамакс с альфа-бета отсечением, таблица транспозиций,
   итеративное углубление, упорядочивание ходов, «спокойный поиск» по взятиям. */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory(require('./content.js'), require('./engine.js'));
  else { root.RZ = root.RZ || {}; Object.assign(root.RZ, factory(root.RZ, root.RZ)); }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (C, E) {
  'use strict';

  const T = C.T;
  const INF = 1e9;
  const MATE = 1e7;
  const VAL = [40, 9, 8, 5, 7, 6]; // core, blade, ward, runner, hook, resonator

  const DEFAULT_W = { aggro: 1.0, defense: 1.0, eco: 1.0, noise: 0 };

  const HP_CACHE = {};
  function hpTable(s) {
    const key = s.faction[0] * 8 + s.faction[1];
    if (HP_CACHE[key]) return HP_CACHE[key];
    const t = [[], []];
    for (let side = 0; side < 2; side++) for (let ty = 0; ty < 6; ty++) t[side][ty] = E.startHp(s, side, ty);
    HP_CACHE[key] = t;
    return t;
  }

  function evaluate(s, w, hpT) {
    w = w || DEFAULT_W;
    const me = s.side, op = 1 - me, b = s.board;
    if (!hpT) hpT = hpTable(s);
    let v = 0;
    for (let i = 0; i < E.MAXU; i++) {
      if (!s.uAlive[i]) continue;
      const side = s.uOwner[i], sign = side === me ? 1 : -1, type = s.uType[i];
      if (type === T.CORE) { v += sign * s.uHp[i] * 6; continue; }
      const maxHp = hpT[side][type];
      const hpRatio = s.uHp[i] / maxHp;
      v += sign * VAL[type] * (0.45 + 0.55 * hpRatio);
      const cell = s.uCell[i];
      const dEnemyCore = E.dist(b, cell, b.base[op]);
      if (dEnemyCore <= 2 && E.atkOf(s, i) > 0) v += sign * 3.0 * w.aggro;
      // инициатива: стоять в контакте с врагом выгодно
      let contact = 0;
      for (let d = 0; d < 6; d++) {
        const nb = b.nb[cell * 6 + d];
        if (nb >= 0 && s.occ[nb] > 0 && s.uOwner[s.occ[nb] - 1] !== side) contact++;
      }
      if (contact) v += sign * contact * 0.9 * w.aggro;
      const dOwnCore = E.dist(b, cell, b.base[side]);
      v += sign * ((6 - dEnemyCore) * 0.55 * w.aggro - Math.max(0, 3 - dOwnCore) * 0.9 * w.defense);
      if (b.node[cell]) v += sign * 1.2 * w.eco;
    }
    v += (s.energy[me] - s.energy[op]) * 1.6 * w.eco;
    v += (E.nodeControl(s, me) - E.nodeControl(s, op)) * 5.5 * w.eco;
    v += (s.od - 1) * 0.4;
    return v;
  }

  /* ---------- хеш ---------- */
  function hash(s) {
    let h = 0x811c9dc5;
    for (let i = 0; i < E.MAXU; i++) {
      if (!s.uAlive[i]) continue;
      h ^= (i * 7 + s.uType[i] * 31 + s.uCell[i] * 131 + s.uHp[i] * 17 + s.uOwner[i] * 3);
      h = Math.imul(h, 0x01000193);
    }
    h ^= (s.side * 977 + s.od * 61 + s.turn * 7 + s.energy[0] * 13 + s.energy[1] * 29);
    h = Math.imul(h, 0x01000193);
    h ^= h >>> 13;
    return h >>> 0;
  }

  function actionKey(a) {
    if (a.t === 'move') return 'm' + a.u + '>' + a.to;
    if (a.t === 'atk') return 'a' + a.u + '>' + a.target;
    if (a.t === 'ab') return 'b' + a.u + a.k + (a.k === 'barrier' ? '>' + a.to : a.target >= 0 ? '>' + a.target : '');
    if (a.t === 'summon') return 's>' + a.to;
    return 'end';
  }

  function orderScore(s, a) {
    const b = s.board, me = s.side;
    let sc = 0;
    if (a.t === 'atk') {
      const tg = a.target;
      const hp = s.uHp[tg], dmg = E.atkOf(s, a.u);
      sc = 60 + Math.min(hp, dmg) * 6 + (hp <= dmg ? 80 + VAL[s.uType[tg]] * 4 : 0);
      if (s.uType[tg] !== T.CORE && E.atkOf(s, tg) > 0 && s.uHp[a.u] <= 1) sc -= 40;
    } else if (a.t === 'ab') {
      if (a.k === 'cleave') sc = 55;
      else if (a.k === 'harpoon') sc = 45;
      else if (a.k === 'impulse') sc = 30;
      else if (a.k === 'mark') sc = 25;
      else if (a.k === 'barrier') sc = 20;
      else sc = 15;
    } else if (a.t === 'move') {
      const u = a.u;
      const dOld = E.dist(b, s.uCell[u], b.base[1 - me]);
      const dNew = E.dist(b, a.to, b.base[1 - me]);
      sc = (dOld - dNew) * 6;
      if (b.node[a.to]) sc += 18;
      sc += 4;
    } else if (a.t === 'summon') sc = 12;
    else if (a.t === 'end') sc = -5;
    return sc;
  }

  function sortByScore(moves) {
    for (let i = 1; i < moves.length; i++) {
      const m = moves[i], sc = m._s;
      let j = i - 1;
      while (j >= 0 && moves[j]._s < sc) { moves[j + 1] = moves[j]; j--; }
      moves[j + 1] = m;
    }
    return moves;
  }

  function orderMoves(s, moves, ttMove, killers) {
    for (let i = 0; i < moves.length; i++) {
      const a = moves[i];
      let sc = orderScore(s, a);
      if (ttMove && E.sameAction(a, ttMove)) sc += 1e6;
      else if (killers && killers.length) {
        const k = actionKey(a);
        if (killers.indexOf(k) >= 0) sc += 900;
      }
      moves[i]._s = sc;
    }
    return sortByScore(moves);
  }

  function Searcher(opts) {
    this.w = opts.w || DEFAULT_W;
    this.hpT = null;
    this.tt = new Map();
    this.nodes = 0;
    this.deadline = Infinity;
    this.stop = false;
    this.path = new Set();
    this.bufs = [];
    this.root = null;
  }

  Searcher.prototype.buf = function (idx, root) {
    let b = this.bufs[idx];
    if (!b) { b = E.clone(root); this.bufs[idx] = b; }
    return b;
  };

  Searcher.prototype.timeUp = function () {
    if (this.stop) return true;
    if ((this.nodes & 511) === 0 && Date.now() > this.deadline) { this.stop = true; return true; }
    return false;
  };

  Searcher.prototype.quiesce = function (s, alpha, beta, qd) {
    this.nodes++;
    if (this.timeUp()) return alpha;
    let stand = evaluate(s, this.w, this.hpT);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    if (qd <= 0) return alpha;
    const moves = E.getMoves(s);
    const caps = [];
    for (let i = 0; i < moves.length; i++) if (moves[i].t === 'atk') caps.push(moves[i]);
    if (!caps.length) return alpha;
    orderMoves(s, caps, null, null);
    for (let i = 0; i < caps.length; i++) {
      if (this.stop) break;
      const n = E.copyState(this.buf(40 + qd, this.root), s);
      E.applyInPlace(n, caps[i]);
      if (n.over) return n.winner === s.side ? MATE : -MATE;
      const sc = -this.quiesce(n, -beta, -alpha, qd - 1);
      if (sc >= beta) return beta;
      if (sc > alpha) alpha = sc;
    }
    return alpha;
  };

  Searcher.prototype.negamax = function (s, depth, alpha, beta, killers) {
    this.nodes++;
    if (this.timeUp()) return alpha;
    if (s.over) return s.winner === s.side ? MATE : (s.winner === -1 ? 0 : -MATE);
    const h = hash(s);
    if (this.path.has(h)) return 0;
    const hit = this.tt.get(h);
    let ttMove = null;
    if (hit) {
      if (hit.d >= depth && hit.flag === 0) return hit.v;
      ttMove = hit.mv;
      if (hit.d >= depth && hit.flag === 1 && hit.v <= alpha) return hit.v;
      if (hit.d >= depth && hit.flag === 2 && hit.v >= beta) return hit.v;
    }
    if (depth <= 0) return this.quiesce(s, alpha, beta, 3);

    const moves = E.getMoves(s);
    orderMoves(s, moves, ttMove, killers[0]);
    const CAP = depth >= 5 ? 34 : depth >= 3 ? 26 : 18;
    if (moves.length > CAP) {
      let endIdx = -1;
      for (let i = 0; i < moves.length; i++) if (moves[i].t === 'end') { endIdx = i; break; }
      const endMove = endIdx >= 0 ? moves.splice(endIdx, 1)[0] : null;
      moves.length = Math.min(moves.length, CAP);
      if (endMove) moves.push(endMove);
    }
    this.path.add(h);
    const nextKillers = [[], killers[0] || []];
    let best = -INF, bestMove = null, flag = 1;
    const startAlpha = alpha;
    for (let i = 0; i < moves.length; i++) {
      const a = moves[i];
      const n = E.copyState(this.buf(depth, this.root), s);
      E.applyInPlace(n, a);
      let sc;
      if (i === 0) sc = -this.negamax(n, depth - 1, -beta, -alpha, nextKillers);
      else {
        sc = -this.negamax(n, depth - 1, -alpha - 1, -alpha, nextKillers);
        if (sc > alpha && sc < beta) sc = -this.negamax(n, depth - 1, -beta, -alpha, nextKillers);
      }
      if (sc > best) { best = sc; bestMove = a; }
      if (sc > alpha) { alpha = sc; flag = 0; }
      if (alpha >= beta) {
        if (a.t === 'atk' || a.t === 'ab') {
          const k = actionKey(a);
          if (killers[0].indexOf(k) < 0) { killers[0].unshift(k); if (killers[0].length > 6) killers[0].pop(); }
        }
        this.path.delete(h);
        this.tt.set(h, { d: depth, v: beta, flag: 2, mv: bestMove });
        return beta;
      }
      if (this.stop) break;
    }
    this.path.delete(h);
    if (best === -INF) return alpha;
    if (this.tt.size > 260000) this.tt.clear();
    this.tt.set(h, { d: depth, v: best, flag: best > startAlpha ? 0 : 1, mv: bestMove });
    return best;
  };

  /* Главная точка входа: выбрать действие.
     level 1..7 — глубина и время; personality — веса. */
  const LEVELS = [
    null,
    { depth: 3, time: 140 },
    { depth: 3, time: 260 },
    { depth: 4, time: 500 },
    { depth: 5, time: 800 },
    { depth: 6, time: 1200 },
    { depth: 7, time: 1800 },
    { depth: 8, time: 2600 }
  ];

  function pickMove(state, opt) {
    opt = opt || {};
    const level = Math.max(1, Math.min(7, opt.level || 3));
    const cfg = LEVELS[level];
    const w = Object.assign({}, DEFAULT_W, opt.w || {});
    const s = E.clone(state);
    const searcher = new Searcher({ w: w });
    searcher.hpT = hpTable(s);
    searcher.root = s;
    searcher.deadline = Date.now() + (opt.time || cfg.time);
    const rootMoves = E.getMoves(s);
    if (!rootMoves.length) return { action: { t: 'end' }, score: 0, depth: 0, nodes: 0 };
    if (rootMoves.length === 1) return { action: rootMoves[0], score: 0, depth: 0, nodes: 0 };

    let ordered = rootMoves.slice();
    orderMoves(s, ordered, null, null);
    let best = ordered[0], bestScore = -INF, reachedDepth = 0;
    const results = [];

    for (let d = 1; d <= cfg.depth; d++) {
      let alpha = -INF, localBest = null, localBestScore = -INF;
      const scored = [];
      for (let i = 0; i < ordered.length; i++) {
        if (searcher.stop) break;
        const a = ordered[i];
        const n = E.copyState(searcher.buf(0, s), s);
        E.applyInPlace(n, a);
        const sc = -searcher.negamax(n, d - 1, -INF, -alpha, [[], []]);
        if (searcher.stop) break;
        scored.push({ a: a, sc: sc });
        if (sc > localBestScore) { localBestScore = sc; localBest = a; }
        if (sc > alpha) alpha = sc;
      }
      if (localBest) {
        best = localBest; bestScore = localBestScore; reachedDepth = d;
        // переупорядочиваем корень по счёту
        scored.sort(function (x, y) { return y.sc - x.sc; });
        const rest = ordered.filter(function (a) { return !scored.some(function (x) { return x.a === a; }); });
        ordered = scored.map(function (x) { return x.a; }).concat(rest);
        results.length = 0;
        scored.forEach(function (x) { results.push(x); });
      }
      if (searcher.stop) break;
      if (bestScore > MATE / 2) break;
    }

    // «человеческая» случайность для слабых ботов
    if (w.noise > 0 && results.length > 1) {
      const pool = results.slice(0, Math.min(results.length, 6));
      const weights = pool.map(function (x) {
        const gap = Math.max(0, (pool[0].sc - x.sc));
        return Math.exp(-gap / Math.max(1, w.noise));
      });
      const total = weights.reduce(function (a, b) { return a + b; }, 0);
      let r = Math.random() * total;
      for (let i = 0; i < pool.length; i++) { r -= weights[i]; if (r <= 0) { best = pool[i].a; bestScore = pool[i].sc; break; } }
    }

    return { action: best, score: Math.round(bestScore), depth: reachedDepth, nodes: searcher.nodes };
  }

  return {
    evaluate: evaluate, pickMove: pickMove, hash: hash, LEVELS: LEVELS,
    actionKey: actionKey, DEFAULT_W: DEFAULT_W
  };
});
