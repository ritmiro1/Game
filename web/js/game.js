/* РЕЗОНАНС — игровой контроллер: ходы, выбор, анимации, бот, режимы */
(function () {
  'use strict';
  const G = {
    active: false, mode: null, cfg: null, state: null, mySide: 0,
    busy: false, selected: -1, targeting: null, actions: [], allMoves: [],
    actionCount: 0, puzzle: null, worker: null, jobs: {}, jobId: 0,
    online: null
  };

  const SIDE = ['#4fd8ff', '#ff6d9d'];

  function store() { return UI.Store.data; }
  function el(id) { return document.getElementById(id); }

  /* ---------------- воркер ИИ ---------------- */
  function initWorker() {
    if (G.worker) return;
    let w = null;
    try {
      if (window.__WORKER_SRC) {
        const blob = new Blob([window.__WORKER_SRC], { type: 'application/javascript' });
        w = new Worker(URL.createObjectURL(blob));
      } else {
        w = new Worker('js/worker.js');
      }
    } catch (e) { w = null; }
    if (!w) return;
    w.onmessage = function (e) {
      const d = e.data, cb = G.jobs[d.id];
      if (cb) { delete G.jobs[d.id]; cb(d); }
    };
    w.onerror = function () { G.worker = null; };
    G.worker = w;
  }

  function askAI(state, level, w) {
    return new Promise(function (resolve) {
      initWorker();
      const payload = { level: level, w: w, state: RZ.encode(state) };
      if (G.worker) {
        const id = ++G.jobId;
        G.jobs[id] = function (d) {
          if (d && d.action) resolve(d);
          else resolve(fallbackAI(state, level, w));
        };
        setTimeout(function () {
          if (G.jobs[id]) { delete G.jobs[id]; resolve(fallbackAI(state, level, w)); }
        }, 12000);
        G.worker.postMessage({ id: id, level: level, w: w, state: payload.state });
      } else {
        setTimeout(function () { resolve(fallbackAI(state, level, w)); }, 40);
      }
    });
  }
  function fallbackAI(state, level, w) {
    try {
      return RZ.pickMove(RZ.decode(RZ.encode(state)), { level: level, w: w });
    } catch (e) {
      const mv = RZ.getMoves(state);
      return { action: mv[Math.floor(Math.random() * mv.length)] || { t: 'end' } };
    }
  }

  /* ---------------- запуск ---------------- */
  G.start = function (cfg) {
    G.cfg = cfg;
    G.mode = cfg.mode || 'bot';
    G.puzzle = cfg.puzzle || null;
    G.mySide = cfg.side === undefined ? 0 : cfg.side;
    G.actionCount = 0;
    G.selected = -1; G.targeting = null; G.busy = false;
    G.finished = false;
    G.online = cfg.online || null;

    if (cfg.state) G.state = RZ.decode(cfg.state);
    else if (G.mode === 'puzzle') G.state = RZ.createPuzzleState(cfg.puzzle);
    else G.state = RZ.createState({ mapId: cfg.mapId, factions: cfg.factions });

    UI.go('game');
    RZRender.setActive(true);
    RZRender.setState(G.state, true);
    RZRender.onPick(onCellClick);
    G.active = true;
    refresh();
    requestAnimationFrame(function () { RZRender.resize(); });

    if (G.mode === 'bot' || G.mode === 'campaign') {
      if (G.state.side !== G.mySide) setTimeout(botTurn, 500);
    }
    if (G.mode === 'online' && G.state.side !== G.mySide) showThink(true, 'противник думает…');
    return G;
  };

  G.quit = function (silent) {
    G.active = false;
    RZRender.setActive(false);
    showThink(false);
    if (!silent) UI.go('menu');
  };

  /* ---------------- HUD ---------------- */
  function coreOf(side) {
    const s = G.state;
    for (let i = 0; i < RZ.MAXU; i++) if (s.uAlive[i] && s.uOwner[i] === side && s.uType[i] === 0) return i;
    return -1;
  }

  function refresh() {
    const s = G.state;
    if (!s) return;
    const btnEnd = document.getElementById('btn-endturn');
    if (btnEnd) btnEnd.disabled = !isMyTurn() || G.busy;
    G.allMoves = (isMyTurn() && !G.busy) ? RZ.getMoves(s) : [];
    if (G.selected >= 0 && (!s.uAlive[G.selected] || s.uOwner[G.selected] !== G.mySide)) G.selected = -1;
    G.actions = G.selected >= 0 ? G.allMoves.filter(function (a) { return a.u === G.selected; }) : [];
    if (G.targeting) {
      G.actions = G.allMoves.filter(function (a) {
        const k = G.targeting.kind;
        if (k === 'barrier') return a.t === 'ab' && a.k === 'barrier';
        if (k === 'mark') return a.t === 'ab' && a.k === 'mark';
        if (k === 'harpoon') return a.t === 'ab' && a.k === 'harpoon';
        if (k === 'summon') return a.t === 'summon';
        return false;
      });
    }
    RZRender.setView({ selected: G.selected, actions: G.actions, targeting: G.targeting });
    updateHud();
    updateAbilityBar();
  }

  function sideName(side) {
    const c = G.cfg || {};
    if (G.mode === 'hotseat') return (c.names && c.names[side]) || (side === 0 ? 'Ткач I' : 'Ткач II');
    if (side === G.mySide) return (c.names && c.names[side]) || store().name;
    return (c.names && c.names[side]) || (c.oppName || 'Противник');
  }
  function sideFaction(side) {
    return G.state.faction[side];
  }

  function updateHud() {
    const s = G.state;
    el('turn-num').textContent = Math.min(s.turn, RZ.TURN_LIMIT);
    const leftSide = G.mode === 'hotseat' ? 1 - s.side : 1 - G.mySide;
    const rightSide = G.mode === 'hotseat' ? s.side : G.mySide;
    // левая карточка = противник
    fillCard('e', leftSide);
    fillCard('m', rightSide);
    const eCard = el('hud-enemy'), mCard = el('hud-me');
    eCard.classList.toggle('active', s.side === leftSide);
    mCard.classList.toggle('active', s.side === rightSide);
    el('sd-warn').textContent = s.turn >= RZ.SUDDEN_DEATH ? 'РАЗЛОМ: урон +1' : ('разлом через ' + (RZ.SUDDEN_DEATH - s.turn) + ' х.');
    // очки действий
    const pips = el('ap-pips');
    let html = '<span>действия</span>';
    for (let i = 0; i < RZ.ACTION_POINTS; i++) {
      html += '<div class="ap ' + (i < s.od && isMyTurn() ? 'on' : 'off') + '">' + (i < s.od ? '●' : '○') + '</div>';
    }
    pips.innerHTML = html;
    if (G.puzzle) {
      const left = G.puzzle.maxActions - G.actionCount;
      pips.innerHTML = '<span>действий осталось</span><b style="color:' + (left < 0 ? '#ff5b6e' : '#4fd8ff') + '">' + Math.max(0, left) + '</b>';
    }
  }

  function fillCard(p, side) {
    const s = G.state;
    const core = coreOf(side);
    const maxHp = RZ.startHp(s, side, 0);
    const hp = core >= 0 ? s.uHp[core] : 0;
    el(p + '-name').textContent = sideName(side);
    const fac = RZ.FACTIONS[sideFaction(side)];
    el(p + '-ava').textContent = fac ? fac.glyph : '◈';
    el(p + '-ava').style.color = SIDE[side];
    el(p + '-ava').style.borderColor = SIDE[side] + '66';
    el(p + '-ava').style.background = SIDE[side] + '1a';
    const units = RZ.unitCount(s, side);
    const nodes = RZ.nodeControl(s, side);
    el(p + '-sub').innerHTML = 'Ядро <b style="color:' + SIDE[side] + '">' + Math.max(0, hp) + '/' + maxHp + '</b> · юнитов ' + units + ' · узлов ' + nodes;
    el(p + '-core').style.width = Math.max(0, (hp / maxHp) * 100) + '%';
    let ep = '';
    for (let i = 0; i < Math.min(10, Math.max(s.energy[side], 6)); i++) {
      ep += '<div class="epip ' + (i < s.energy[side] ? 'full' : '') + '"></div>';
    }
    el(p + '-energy').innerHTML = ep;
  }

  function updateAbilityBar() {
    const bar = el('ability-bar');
    const s = G.state;
    let html = '';
    if (isMyTurn() && !G.busy) {
      if (G.selected >= 0) {
        const u = G.selected, type = s.uType[u], t = RZ.TYPES[type];
        const ab = t.ability;
        const hasAb = G.allMoves.some(function (a) { return a.t === 'ab' && a.u === u && (!ab || a.k === ab.id); });
        const cd = s.uCd[u];
        const abName = ab ? ab.name : '—';
        const cost = ab ? ab.cost : 0;
        html += '<button class="abtn" data-ab="ability" ' + (hasAb ? '' : 'disabled') + '>' +
          '<span>' + abName + '</span>' +
          (cost ? '<span class="cost">' + cost + '⚡</span>' : '') +
          (cd > 0 ? '<span class="cost" style="color:#ff9aa8;border-color:#ff9aa866">кд ' + cd + '</span>' : '') +
          '</button>';
        html += '<span class="tagline" style="margin:0 6px">' + t.name + ': HP ' + s.uHp[u] + '/' + RZ.startHp(s, s.uOwner[u], type) +
          (t.atk ? ' · урон ' + RZ.atkOf(s, u) : '') + '</span>';
      }
      const canSummon = G.allMoves.some(function (a) { return a.t === 'summon'; });
      if (canSummon) html += '<button class="abtn" data-ab="summon"><span>Призвать гонца</span><span class="cost">' + RZ.SUMMON_COST + '⚡</span></button>';
      if (!G.selected && !G.targeting) html += '<span class="tagline">выбери своего юнита на арене</span>';
      if (G.targeting) html += '<button class="abtn" data-ab="cancel"><span>Отмена</span></button><span class="tagline">выбери цель</span>';
    } else if (G.busy) {
      html += '<span class="tagline">…</span>';
    }
    bar.innerHTML = html;
    UI.$$('[data-ab]', bar).forEach(function (b) {
      b.addEventListener('click', function () {
        const k = b.getAttribute('data-ab');
        if (k === 'cancel') { G.targeting = null; refresh(); return; }
        RZAudio.play('select');
        if (k === 'summon') { G.selected = -1; G.targeting = { kind: 'summon' }; refresh(); }
        else if (k === 'ability') {
          const u = G.selected, ab = RZ.TYPES[G.state.uType[u]].ability;
          if (!ab) return;
          if (ab.id === 'barrier') { G.targeting = { kind: 'barrier' }; refresh(); }
          else if (ab.id === 'mark') { G.targeting = { kind: 'mark' }; refresh(); }
          else if (ab.id === 'harpoon') { G.targeting = { kind: 'harpoon' }; refresh(); }
          else {
            const a = G.allMoves.filter(function (x) { return x.t === 'ab' && x.u === u; })[0];
            if (a) exec(a);
          }
        }
      });
    });
  }

  function showThink(v, txt) {
    const t = el('think');
    if (!t) return;
    t.classList.toggle('hide', !v);
    if (txt) el('think-txt').textContent = txt;
  }

  /* ---------------- ввод ---------------- */
  function isMyTurn() {
    if (!G.state || G.state.over) return false;
    if (G.mode === 'hotseat') return true;
    if (G.mode === 'puzzle') return G.state.side === 0;
    return G.state.side === G.mySide;
  }

  function selectUnit(u) {
    G.selected = u;
    G.targeting = null;
    RZAudio.play('select');
    refresh();
  }
  function deselect() { G.selected = -1; G.targeting = null; refresh(); }

  function onCellClick(cell) {
    if (!G.active || G.busy || !isMyTurn() || G.state.over) return;
    const s = G.state;
    if (cell < 0) { deselect(); return; }
    if (G.targeting) {
      const a = G.actions.filter(function (x) {
        if (x.t === 'ab' && x.k === 'barrier') return x.to === cell;
        if (x.t === 'ab' && (x.k === 'mark' || x.k === 'harpoon')) return x.target !== undefined && s.uCell[x.target] === cell;
        if (x.t === 'summon') return x.to === cell;
        return false;
      })[0];
      if (a) return exec(a);
      G.targeting = null; refresh(); return;
    }
    const occ = s.occ[cell];
    if (occ > 0 && s.uOwner[occ - 1] === s.side) { selectUnit(occ - 1); return; }
    // перемещение / призыв на клетку
    let a = G.actions.filter(function (x) { return (x.t === 'move' || x.t === 'summon') && (x.to === cell); })[0];
    if (a) return exec(a);
    // атака
    if (occ > 0 && G.selected >= 0) {
      a = G.actions.filter(function (x) { return x.t === 'atk' && x.target === occ - 1; })[0];
      if (a) return exec(a);
    }
    // клик по чужому юниту без выбора — попробуем выбрать того, кто может ударить
    if (occ > 0) {
      const attacker = G.allMoves.filter(function (x) { return x.t === 'atk' && x.target === occ - 1; })[0];
      if (attacker) { selectUnit(attacker.u); UI.toast('Теперь выбери цель ещё раз или нажми юнита рядом'); }
      return;
    }
    deselect();
  }

  G.reselectSide = function () { refresh(); };

  /* ---------------- выполнение ---------------- */
  function exec(action) {
    if (G.busy) return;
    return doAction(action).then(afterAction);
  }

  async function doAction(action) {
    const keepU = action.u;
    G.busy = true;
    G.selected = -1; G.targeting = null;
    G.actionCount++;
    let ev;
    try {
      if (!RZ.isLegal(G.state, action)) {
        const mv = RZ.getMoves(G.state);
        action = mv[0] || { t: 'end' };
      }
      ev = RZ.applyInPlace(G.state, action);
    } catch (e) {
      console.error(e);
      ev = [];
    }
    G.history = G.history || [];
    G.history.push(action);
    if (G.mode === 'online' && G.online) {
      const a = action;
      try { if (window.Net) Net.sendAction(a); } catch (err) { }
    }
    refresh();
    await RZRender.playEvents(ev);
    G.busy = false;
    if (keepU !== undefined && keepU >= 0 && !G.state.over && G.state.side === G.mySide &&
      G.state.od > 0 && G.state.uAlive[keepU] && G.state.uOwner[keepU] === G.mySide) {
      G.selected = keepU;
      refresh();
    }
    if (G.online && G.mode === 'online' && action.t !== 'end') { /* сервер всё равно пришлёт состояние */ }
    return action;
  }

  function afterAction() {
    if (!G.active) return;
    const s = G.state;
    if (s.over) { finish(); return; }
    if (G.mode === 'puzzle') {
      if (s.side !== 0) {
        // в задачах соперник не действует — просто отдаём ход обратно
        RZ.endTurn(s, []);
      }
      if (!G.state.over && G.actionCount >= G.puzzle.maxActions) { finish(); return; }
      refresh();
      return;
    }
    refresh();
    if (G.mode === 'bot' || G.mode === 'campaign') {
      if (s.side !== G.mySide && !s.over) setTimeout(botTurn, 260);
    } else if (G.mode === 'hotseat') {
      if (!s.over) {
        updateHud();
      }
    } else if (G.mode === 'online') {
      if (s.side !== G.mySide && !s.over) showThink(true, 'противник думает…');
      else showThink(false);
    }
  }

  /* ---------------- бот ---------------- */
  async function botTurn() {
    if (!G.active || G.busy) return;
    const s = G.state;
    const botSide = 1 - G.mySide;
    if (s.over || s.side !== botSide) { showThink(false); return; }
    showThink(true, (G.cfg.botName || 'бот') + ' думает…');
    G.busy = true;
    await new Promise(function (r) { setTimeout(r, 180); });
    const level = G.cfg.botLevel || 3;
    const w = G.cfg.botW || null;
    const res = await askAI(G.state, level, w);
    G.busy = false;
    if (!G.active) return;
    let action = res && res.action ? res.action : { t: 'end' };
    if (!RZ.isLegal(G.state, action)) {
      const mv = RZ.getMoves(G.state);
      action = mv[Math.floor(Math.random() * mv.length)] || { t: 'end' };
    }
    showThink(false);
    await doAction(action);
    afterAction();
  }

  /* ---------------- завершение ---------------- */
  function finish() {
    const s = G.state;
    if (!G.active || G.finished) return;
    G.finished = true;
    G.active = false;
    RZRender.setActive(false);
    showThink(false);
    let res; // 'win' | 'lose' | 'draw'
    if (s.winner === -1) res = 'draw';
    else if (G.mode === 'hotseat') res = 'win';
    else res = s.winner === G.mySide ? 'win' : 'lose';
    if (G.mode === 'puzzle') res = s.winner === 0 ? 'win' : 'lose';

    const d = store();
    let extra = '';
    if (G.mode === 'campaign' && res === 'win') {
      const id = G.cfg.chapter.id;
      if (!d.campaign[id]) { d.campaign[id] = 1; extra = '<div class="tagline" style="margin-top:8px;color:#b6ff5f">Глава пройдена · +' + G.cfg.chapter.reward + ' опыта контура</div>'; }
    }
    if (G.mode === 'puzzle' && res === 'win') {
      if (!d.puzzles[G.puzzle.id]) d.puzzles[G.puzzle.id] = 1;
      extra = '<div class="tagline" style="margin-top:8px;color:#b6ff5f">Испытание решено</div>';
    }
    if (G.mode === 'bot' || G.mode === 'campaign' || G.mode === 'hotseat' || G.mode === 'puzzle') {
      if (G.mode !== 'hotseat' && G.mode !== 'puzzle') {
        d.games++;
        if (res === 'win') { d.wins++; d.streak = (d.streak || 0) + 1; } else d.streak = 0;
      }
    }
    d.best = Math.max(d.best || 0, d.streak || 0);
    UI.Store.save();
    if (window.App && App.updateTopbar) App.updateTopbar();

    if (res === 'win') RZAudio.play('win');
    else if (res === 'lose') RZAudio.play('lose');
    else RZAudio.play('draw');

    const title = res === 'win' ? 'КОНТУР ЗАМКНУТ' : res === 'lose' ? 'КОНТУР РАЗОРВАН' : 'НИЧЬЯ';
    const cls = res === 'win' ? 'win' : res === 'lose' ? 'lose' : 'draw';
    const why = s.winner === -1 ? 'Ничья по контролю.' :
      (coreOf(s.winner) < 0 ? 'Ядро уничтожено.' : 'Победа по контролю на 40-м ходу.');
    setTimeout(function () {
      UI.modal({
        title: '',
        body: '<div class="result-big ' + cls + '">' + title + '</div>' +
          '<div style="text-align:center" class="muted">' + why + extra + '</div>' +
          '<div class="divider"></div>' +
          '<div class="row" style="justify-content:center;gap:18px">' +
          '<div style="text-align:center"><div class="turn-lbl">ходов</div><div class="turn-num" style="font-size:18px">' + s.turn + '</div></div>' +
          '<div style="text-align:center"><div class="turn-lbl">действий</div><div class="turn-num" style="font-size:18px">' + G.actionCount + '</div></div>' +
          '<div style="text-align:center"><div class="turn-lbl">контроль</div><div class="turn-num" style="font-size:18px">' + Math.round(RZ.score(s, G.mySide)) + '</div></div>' +
          '</div>',
        actions: [
          { label: 'Реванш', cls: 'primary', onClick: function () { restart(); } },
          { label: 'В меню', onClick: function () { UI.go('menu'); } }
        ]
      });
    }, 420);
  }

  function restart() {
    const cfg = Object.assign({}, G.cfg);
    if (cfg.mode === 'puzzle') cfg.state = null;
    G.start(cfg);
  }

  /* ---------------- горячие клавиши ---------------- */
  document.addEventListener('keydown', function (e) {
    if (!G.active || UI.modalOpen()) return;
    if (e.code === 'Space') {
      e.preventDefault();
      if (!isMyTurn() || G.busy) return;
      const end = G.allMoves.filter(function (a) { return a.t === 'end'; })[0];
      if (end) exec(end);
    } else if (e.code === 'Escape') {
      deselect();
    } else if (e.code === 'KeyE') {
      if (!isMyTurn() || G.busy) return;
      const end = G.allMoves.filter(function (a) { return a.t === 'end'; })[0];
      if (end) exec(end);
    }
  });

  document.getElementById('btn-endturn').addEventListener('click', function () {
    if (!G.active || !isMyTurn() || G.busy) return;
    const end = G.allMoves.filter(function (a) { return a.t === 'end'; })[0];
    if (end) exec(end);
  });
  document.getElementById('btn-undo').addEventListener('click', function () { if (!G.busy) deselect(); });
  document.getElementById('btn-quit-game').addEventListener('click', function () {
    UI.modal({
      title: 'Выйти из дуэли?',
      sub: 'Текущая партия будет прервана.',
      actions: [
        { label: 'Остаться', keepOpen: true, onClick: function () { UI.closeModal(); } },
        {
          label: 'Выйти', cls: 'pink', onClick: function () {
            if (G.mode === 'online' && window.Net) Net.resign();
            G.quit();
          }
        }
      ]
    });
  });

  window.Game = G;
  window.GameUI = { refresh: refresh, showThink: showThink, updateHud: updateHud, finish: finish, isMyTurn: isMyTurn };

  /* сервер прислал ход противника */
  G.applyRemote = function (action, stateEnc) {
    G.busy = true;
    G.selected = -1; G.targeting = null;
    let ev = [];
    try { ev = RZ.applyInPlace(G.state, action); } catch (e) { }
    if (stateEnc) G.state = RZ.decode(stateEnc);
    RZRender.setState(G.state, false);
    refresh();
    return RZRender.playEvents(ev).then(function () {
      G.busy = false;
      if (G.state.over) { finish(); return; }
      refresh();
      showThink(G.state.side !== G.mySide, 'противник думает…');
    });
  };
  G.onlineStart = function (stateEnc, side) {
    G.mySide = side;
    G.state = RZ.decode(stateEnc);
    RZRender.setState(G.state, true);
    refresh();
    showThink(G.state.side !== G.mySide, 'противник думает…');
  };
  G.sendActionHook = null;
})();
