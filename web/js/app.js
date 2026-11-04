/* РЕЗОНАНС — приложение: навигация, меню, контент, онлайн-интерфейс */
(function () {
  'use strict';
  const App = {};
  const C = window.RZ;
  const store = function () { return UI.Store.data; };
  const el = function (id) { return document.getElementById(id); };

  App.onScreen = function (name) {
    if (name === 'game') { RZRender.setActive(true); RZRender.resize(); }
    else if (name === 'online') { renderLobby(); renderTop(); if (!Net.connected) Net.status('Не подключено'); }
    else if (!Game.active) RZRender.setActive(false);
  };

  /* ---------------- верхняя панель ---------------- */
  App.updateTopbar = function () {
    const d = store();
    el('tb-name').textContent = d.name;
    el('tb-wins').textContent = d.wins;
    el('tb-games').textContent = d.games;
    el('tb-elo').textContent = (d.online && d.online.elo) ? d.online.elo : d.elo;
    el('camp-progress').textContent = Object.keys(d.campaign || {}).length + '/' + C.CAMPAIGN.length;
    el('puz-progress').textContent = Object.keys(d.puzzles || {}).length + '/' + C.PUZZLES.length;
    el('profile-line').textContent = d.name + ' · рейтинг ' + ((d.online && d.online.elo) || d.elo) +
      ' · игр ' + d.games + ' · побед ' + d.wins;
  };

  /* ---------------- контент ---------------- */
  function fillSelect(sel, items, value, label) {
    if (!sel) return;
    sel.innerHTML = items.map(function (it, i) {
      const v = typeof it === 'object' ? it.value : i;
      const l = typeof it === 'object' ? it.label : it;
      return '<option value="' + v + '"' + (String(v) === String(value) ? ' selected' : '') + '>' + UI.esc(l) + '</option>';
    }).join('');
  }

  function buildBots() {
    const root = el('bots-list');
    root.innerHTML = C.BOTS.map(function (b, i) {
      const f = C.FACTIONS[b.faction];
      const bars = [1, 2, 3, 4, 5, 6, 7].map(function (n) {
        return '<i class="' + (n <= b.level ? 'on' : '') + '"></i>';
      }).join('');
      return '<div class="card" data-bot="' + i + '">' +
        '<span class="num">УРОВЕНЬ ' + b.level + ' · рейтинг ' + b.elo + '</span>' +
        '<h4>' + b.avatar + ' ' + UI.esc(b.name) + '</h4>' +
        '<p>' + UI.esc(b.desc) + '</p>' +
        '<div class="diff">' + bars + '</div>' +
        '<div class="tagline" style="margin-top:8px;color:' + f.color + '">' + UI.esc(f.name) + '</div>' +
        '</div>';
    }).join('');
    UI.$$('[data-bot]', root).forEach(function (c) {
      c.addEventListener('click', function () {
        RZAudio.play('click');
        startBot(+c.getAttribute('data-bot'));
      });
    });
  }

  function startBot(i) {
    const b = C.BOTS[i];
    const mapId = el('sel-map-bot').value;
    const fac = +el('sel-faction-bot').value;
    Game.start({
      mode: 'bot', mapId: mapId, factions: [fac, b.faction],
      botLevel: b.level, botW: Object.assign({}, b.w), botName: b.name,
      names: [store().name, b.name]
    });
  }

  function buildCampaign() {
    const root = el('campaign-list');
    const d = store();
    root.innerHTML = C.CAMPAIGN.map(function (ch, i) {
      const prev = i > 0 ? C.CAMPAIGN[i - 1] : null;
      const locked = prev && !d.campaign[prev.id];
      const done = !!d.campaign[ch.id];
      const bot = C.BOTS.filter(function (b) { return b.id === ch.bot; })[0];
      return '<div class="card' + (locked ? ' locked' : '') + '" data-ch="' + i + '">' +
        (done ? '<div class="done">✔</div>' : '') +
        '<span class="num">ГЛАВА ' + (i + 1) + '</span>' +
        '<h4>' + UI.esc(ch.name) + '</h4>' +
        '<p>' + UI.esc(ch.brief) + '</p>' +
        '<div class="tagline" style="margin-top:8px">' + UI.esc(C.MAP_BY_ID[ch.map].name) + ' · ' + UI.esc(bot.name) + ' (ур. ' + bot.level + ')</div>' +
        '</div>';
    }).join('');
    UI.$$('[data-ch]', root).forEach(function (c) {
      c.addEventListener('click', function () {
        if (c.classList.contains('locked')) { UI.toast('Сначала пройди предыдущую главу', 'err'); RZAudio.play('error'); return; }
        openChapter(+c.getAttribute('data-ch'));
      });
    });
  }

  function openChapter(i) {
    const ch = C.CAMPAIGN[i];
    const bot = C.BOTS.filter(function (b) { return b.id === ch.bot; })[0];
    const fac = C.FACTIONS[ch.playerFaction];
    UI.modal({
      title: 'Глава ' + (i + 1) + '. ' + UI.esc(ch.name),
      sub: UI.esc(C.MAP_BY_ID[ch.map].name) + ' · противник: ' + UI.esc(bot.name),
      body: '<p style="line-height:1.6">' + UI.esc(ch.brief) + '</p>' +
        '<div class="panel" style="margin-top:14px;padding:12px">' +
        '<div class="tagline">ЦЕЛЬ</div><div>' + UI.esc(ch.goal) + '</div>' +
        '<div class="tagline" style="margin-top:8px">СОВЕТ</div><div>' + UI.esc(ch.tip) + '</div>' +
        '<div class="tagline" style="margin-top:8px">ТВОЯ ФРАКЦИЯ</div><div style="color:' + fac.color + '">' + UI.esc(fac.name) + '</div>' +
        '</div>',
      actions: [
        { label: 'Назад', keepOpen: true, onClick: function () { UI.closeModal(); } },
        {
          label: 'В контур', cls: 'primary', onClick: function () {
            Game.start({
              mode: 'campaign', mapId: ch.map, factions: [ch.playerFaction, bot.faction],
              botLevel: bot.level, botW: Object.assign({}, bot.w), botName: bot.name,
              chapter: ch, names: [store().name, bot.name]
            });
          }
        }
      ]
    });
  }

  function buildPuzzles() {
    const root = el('puzzle-list');
    const d = store();
    root.innerHTML = C.PUZZLES.map(function (p, i) {
      const done = !!d.puzzles[p.id];
      return '<div class="card" data-pz="' + i + '">' +
        (done ? '<div class="done">✔</div>' : '') +
        '<span class="num">ЗАДАЧА ' + (i + 1) + ' · ' + p.maxActions + ' действий</span>' +
        '<h4>' + UI.esc(p.name) + '</h4>' +
        '<p>' + UI.esc(p.text) + '</p>' +
        '</div>';
    }).join('');
    UI.$$('[data-pz]', root).forEach(function (c) {
      c.addEventListener('click', function () { openPuzzle(+c.getAttribute('data-pz')); });
    });
  }

  function openPuzzle(i) {
    const p = C.PUZZLES[i];
    Game.start({ mode: 'puzzle', puzzle: p, side: 0, names: [store().name, 'Задача'], botName: 'Задача' });
    setTimeout(function () {
      UI.toast(p.text);
    }, 300);
  }

  function buildRules() {
    const root = el('rules-body');
    let html = '<div class="grid2">';
    html += '<div class="panel"><h2>Как играть</h2>';
    C.LORE.rules.forEach(function (r) { html += '<div class="muted" style="margin-bottom:8px">• ' + UI.esc(r) + '</div>'; });
    html += '<div class="divider"></div><h2>Разлом</h2>' +
      '<div class="muted">С ' + 24 + '-го хода контур нестабилен: любой урон увеличивается на 1. Урон по Ядру всегда удвоен — Ядро резонирует.</div>' +
      '<div class="muted" style="margin-top:8px">Призыв Гонца стоит ' + 4 + ' энергии, одновременно под твоим началом не больше ' + 8 + ' юнитов.</div>';
    html += '</div>';
    html += '<div class="panel"><h2>Фракции</h2>';
    C.FACTIONS.forEach(function (f) {
      html += '<div style="margin-bottom:14px"><div style="color:' + f.color + ';font-weight:800">' + f.glyph + ' ' + UI.esc(f.name) + '</div>' +
        '<div class="muted">' + UI.esc(f.desc) + '</div>' +
        '<div class="tagline" style="margin-top:4px">' + f.passives.map(UI.esc).join(' · ') + '</div></div>';
    });
    html += '</div></div>';
    html += '<div class="panel" style="margin-top:16px"><h2>Юниты</h2><div class="units-grid">';
    C.TYPES.forEach(function (t) {
      html += '<div class="unit-card"><div class="uh"><div class="gl">' + t.glyph + '</div><div><b>' + UI.esc(t.name) + '</b></div></div>' +
        '<div class="muted" style="font-size:11.5px">' + UI.esc(t.lore) + '</div>' +
        '<div class="st"><span>HP <b>' + t.hp + '</b></span>' +
        (t.atk ? '<span>урон <b>' + t.atk + '</b> (' + t.atkCost + '⚡)</span>' : '<span>не атакует</span>') +
        '<span>ход <b>' + (t.move ? t.move : '—') + '</b></span>' +
        (t.ability ? '<span>' + UI.esc(t.ability.name) + ' <b>' + (t.ability.cost ? t.ability.cost + '⚡' : 'бесплатно') + '</b>, кд ' + t.ability.cd + '</span>' : '') +
        '</div></div>';
    });
    html += '</div></div>';
    html += '<div class="panel" style="margin-top:16px"><h2>Арены</h2><div class="cards">';
    C.MAPS.forEach(function (m) {
      html += '<div class="card" style="cursor:default"><span class="num">' + UI.esc(m.tag) + '</span><h4>' + UI.esc(m.name) + '</h4><p>' + UI.esc(m.desc) + '</p></div>';
    });
    html += '</div></div>';
    root.innerHTML = html;
  }

  /* ---------------- онлайн ---------------- */
  function renderLobby() {
    const root = el('lobby-list');
    const rooms = Net.lobby || [];
    if (!Net.connected) {
      root.innerHTML = '<div class="muted">Подключись к серверу, чтобы увидеть дуэли.</div>';
      return;
    }
    if (!rooms.length) {
      root.innerHTML = '<div class="muted">Открытых дуэлей нет. Создай свою — и жди вызова.</div>';
      return;
    }
    root.innerHTML = rooms.map(function (r) {
      const fac = C.FACTIONS[r.faction] || C.FACTIONS[0];
      return '<div class="lobby-row">' +
        '<div class="ava" style="width:30px;height:30px;font-size:14px;color:' + fac.color + '">' + fac.glyph + '</div>' +
        '<div class="grow"><div class="nm">' + UI.esc(r.name) + '</div>' +
        '<div class="meta">' + UI.esc(r.map) + ' · рейтинг ' + r.elo + ' · ' + (r.status === 'playing' ? 'идёт дуэль' : 'ждёт соперника') + '</div></div>' +
        (r.id === (Net.room && Net.room.id) ? '<span class="tag">твоя</span>' :
          (r.status === 'waiting' ? '<button class="btn sm primary" data-join="' + r.id + '">Войти</button>' : '<span class="tag pink">занято</span>')) +
        '</div>';
    }).join('');
    UI.$$('[data-join]', root).forEach(function (b) {
      b.addEventListener('click', function () { Net.join(b.getAttribute('data-join')); });
    });
  }

  function renderTop() {
    const root = el('lb-list');
    const list = Net.top || [];
    if (!list.length) {
      root.innerHTML = '<div class="muted">Рейтинг появится после подключения.</div>';
      return;
    }
    let html = '<table class="lb"><thead><tr><th></th><th>Ткач</th><th>Рейтинг</th><th>И</th></tr></thead><tbody>';
    list.forEach(function (p, i) {
      html += '<tr><td class="rank ' + (i === 0 ? 'top1' : i === 1 ? 'top2' : i === 2 ? 'top3' : '') + '">' + (i + 1) + '</td>' +
        '<td>' + UI.esc(p.name) + (p.online ? ' <span style="color:#b6ff5f">•</span>' : '') + '</td>' +
        '<td><b>' + p.elo + '</b></td><td>' + p.games + '</td></tr>';
    });
    html += '</tbody></table>';
    root.innerHTML = html;
  }

  function chatAdd(name, text, sys, pink) {
    const log = el('chat-log');
    const m = UI.el('div', 'm' + (sys ? ' sys' : '') + (pink ? ' pink' : ''));
    m.innerHTML = sys ? UI.esc(text) : '<b>' + UI.esc(name) + ':</b> ' + UI.esc(text);
    log.appendChild(m);
    log.scrollTop = log.scrollHeight;
    while (log.children.length > 120) log.removeChild(log.firstChild);
  }

  function bindNet() {
    Net.onStatus = function (txt, err) {
      el('online-status').textContent = txt;
      el('online-status').style.color = err ? '#ff8f9e' : '';
      if (err) UI.toast(txt, 'err');
      renderTop();
    };
    Net.onLobby = function () { renderLobby(); };
    Net.onTop = function (list) { renderTop(); };
    Net.onChat = function (m) { chatAdd(m.name, m.m, m.sys); };
    Net.onRoom = function (m) {
      UI.toast('Дуэль создана. Ждём соперника…');
      renderLobby();
      UI.modal({
        title: 'Ожидание соперника',
        sub: 'Комната «' + UI.esc(m.name || 'дуэль') + '» · ' + UI.esc(m.map || '') ,
        body: '<div class="muted">Отправь другу ссылку на этот сервер — он увидит твою дуэль в лобби и сможет присоединиться.</div>',
        actions: [{ label: 'Отменить', cls: 'ghost', onClick: function () { Net.leave(); } }],
        onClose: function () { }
      });
    };
    Net.onStart = function (m) {
      UI.closeModal();
      UI.toast('Дуэль началась!');
      Game.start({
        mode: 'online', state: m.state, side: m.side, online: true,
        names: [m.players[0].name, m.players[1].name]
      });
    };
    Net.onApplied = function (m) {
      if (!Game.active || Game.mode !== 'online') return;
      if (m.side === Game.mySide) return;
      Game.applyRemote(m.a, m.state);
    };
    Net.onSync = function (m) {
      if (Game.mode !== 'online') return;
      Game.state = RZ.decode(m.state);
      RZRender.setState(Game.state, false);
      GameUI.refresh();
    };
    Net.onOver = function (m) {
      const d = store();
      if (m.elo) { d.online = d.online || {}; d.online.elo = m.elo; d.online.games = (d.online.games || 0) + 1; if (m.winner === Game.mySide) d.online.wins = (d.online.wins || 0) + 1; }
      if (m.top) Net.top = m.top;
      UI.Store.save();
      App.updateTopbar();
      renderTop();
      if (Game.mode === 'online') {
        if (m.state) {
          Game.state = RZ.decode(m.state);
          RZRender.setState(Game.state, false);
        }
        GameUI.finish();
      }
    };
    Net.onLeft = function () { UI.toast('Ты покинул лобби'); renderLobby(); };
    Net.onErr = function (msg) { };
  }

  /* ---------------- профиль ---------------- */
  function renderProfile() {
    const d = store();
    el('inp-profile-name').value = d.name;
    fillSelect(el('sel-profile-faction'), C.FACTIONS.map(function (f, i) { return { value: i, label: f.name }; }), d.faction);
    fillSelect(el('sel-profile-level'), C.BOTS.map(function (b, i) { return { value: b.level, label: 'Уровень ' + b.level + ' · ' + b.name }; }), d.level);
    const stats = [
      ['Побед против ботов', d.wins],
      ['Всего дуэлей', d.games],
      ['Серия побед', d.streak || 0],
      ['Лучшая серия', d.best || 0],
      ['Сетевой рейтинг', (d.online && d.online.elo) || 1200],
      ['Глав кампании', Object.keys(d.campaign || {}).length + ' / ' + C.CAMPAIGN.length],
      ['Задач решено', Object.keys(d.puzzles || {}).length + ' / ' + C.PUZZLES.length]
    ];
    el('profile-stats').innerHTML = stats.map(function (s) {
      return '<div class="split" style="padding:7px 0;border-bottom:1px solid rgba(110,190,255,.08)">' +
        '<span class="muted">' + s[0] + '</span><b>' + s[1] + '</b></div>';
    }).join('');
    el('btn-toggle-sound').textContent = 'Звук: ' + (d.sound ? 'вкл' : 'выкл');
    el('btn-toggle-music').textContent = 'Эмбиент: ' + (d.music ? 'вкл' : 'выкл');
  }

  /* ---------------- горячая посадка ---------------- */
  function startHotseat() {
    const opts = C.MAPS.map(function (m) { return { value: m.id, label: m.name }; });
    const body = '<div class="col">' +
      '<div><label class="fl">Арена</label><select id="hs-map">' + opts.map(function (o) { return '<option value="' + o.value + '">' + UI.esc(o.label) + '</option>'; }).join('') + '</select></div>' +
      '<div><label class="fl">Фракция первого Ткача</label><select id="hs-f0">' + C.FACTIONS.map(function (f, i) { return '<option value="' + i + '">' + UI.esc(f.name) + '</option>'; }).join('') + '</select></div>' +
      '<div><label class="fl">Фракция второго Ткача</label><select id="hs-f1">' + C.FACTIONS.map(function (f, i) { return '<option value="' + i + '"' + (i === 1 ? ' selected' : '') + '>' + UI.esc(f.name) + '</option>'; }).join('') + '</select></div>' +
      '</div>';
    UI.modal({
      title: 'Дуэль на одном экране',
      sub: 'Два Ткача, один контур, никакой пощады.',
      body: body,
      actions: [
        { label: 'Отмена', keepOpen: true, onClick: function () { UI.closeModal(); } },
        {
          label: 'Начать', cls: 'primary', keepOpen: true, onClick: function (box) {
            const mapId = box.querySelector('#hs-map').value;
            const f0 = +box.querySelector('#hs-f0').value, f1 = +box.querySelector('#hs-f1').value;
            UI.closeModal();
            Game.start({ mode: 'hotseat', mapId: mapId, factions: [f0, f1], names: ['Ткач I', 'Ткач II'] });
          }
        }
      ]
    });
  }

  function openSettings() {
    const d = store();
    UI.modal({
      title: 'Настройки',
      body: '<div class="col">' +
        '<div class="row"><button class="btn sm" id="st-sound">Звук: ' + (d.sound ? 'вкл' : 'выкл') + '</button>' +
        '<button class="btn sm" id="st-music">Эмбиент: ' + (d.music ? 'вкл' : 'выкл') + '</button></div>' +
        '<div><label class="fl">Сервер для онлайна</label><input id="st-server" value="' + UI.esc(d.server || '') + '" placeholder="' + UI.esc(Net.defaultUrl()) + '"></div>' +
        '<div class="muted">Игра работает офлайн против ботов. Для сетевых дуэлей запусти сервер (server/server.js) или подключись к любому доступному.</div>' +
        '</div>',
      actions: [{ label: 'Готово', cls: 'primary', onClick: function () { } }]
    });
    const box = UI.$('.modal');
    box.querySelector('#st-sound').addEventListener('click', function (e) {
      d.sound = !d.sound; RZAudio.setSound(d.sound); UI.Store.save();
      e.target.textContent = 'Звук: ' + (d.sound ? 'вкл' : 'выкл');
    });
    box.querySelector('#st-music').addEventListener('click', function (e) {
      d.music = !d.music; RZAudio.setMusic(d.music); UI.Store.save();
      e.target.textContent = 'Эмбиент: ' + (d.music ? 'вкл' : 'выкл');
    });
    box.querySelector('#st-server').addEventListener('change', function (e) {
      d.server = e.target.value.trim(); UI.Store.save();
    });
  }

  /* ---------------- инициализация ---------------- */
  App.init = function () {
    UI.Store.load();
    const d = store();
    RZRender.init(el('board'));
    RZRender.startBackground();

    // селекты
    fillSelect(el('sel-map-bot'), C.MAPS.map(function (m) { return { value: m.id, label: m.name }; }), 'core');
    fillSelect(el('sel-faction-bot'), C.FACTIONS.map(function (f, i) { return { value: i, label: f.name }; }), d.faction);
    fillSelect(el('sel-faction-online'), C.FACTIONS.map(function (f, i) { return { value: i, label: f.name }; }), d.faction);
    el('sel-faction-bot').addEventListener('change', function () { d.faction = +this.value; UI.Store.save(); });
    el('sel-faction-online').addEventListener('change', function () { d.faction = +this.value; UI.Store.save(); Net.send({ t: 'faction', faction: d.faction }); });
    el('inp-name').value = d.name;
    el('inp-server').value = d.server || Net.defaultUrl();

    buildBots();
    buildCampaign();
    buildPuzzles();
    buildRules();
    bindNet();
    App.updateTopbar();

    // навигация
    UI.$$('[data-go]').forEach(function (b) {
      b.addEventListener('click', function () {
        RZAudio.play('click');
        const go = b.getAttribute('data-go');
        if (go === 'hotseat') return startHotseat();
        if (go === 'settings') return openSettings();
        if (go === 'profile') { renderProfile(); return UI.go('profile'); }
        UI.go(go);
      });
    });

    // онлайн-кнопки
    el('btn-reconnect').addEventListener('click', function () {
      const url = (el('inp-server').value || '').trim() || Net.defaultUrl();
      d.server = url; UI.Store.save();
      Net.connect(url);
    });
    el('btn-create-room').addEventListener('click', function () {
      if (!Net.connected) { UI.toast('Сначала подключись к серверу', 'err'); return; }
      Net.createRoom();
    });
    el('btn-quick').addEventListener('click', function () {
      if (!Net.connected) { UI.toast('Сначала подключись к серверу', 'err'); return; }
      Net.quick();
      UI.toast('Ищем соперника по рейтингу…');
    });
    el('btn-refresh-lobby').addEventListener('click', function () { Net.send({ t: 'lobby' }); });
    el('btn-chat').addEventListener('click', sendChat);
    el('inp-chat').addEventListener('keydown', function (e) { if (e.key === 'Enter') sendChat(); });
    function sendChat() {
      const v = el('inp-chat').value.trim();
      if (!v) return;
      if (!Net.connected) { UI.toast('Нет соединения', 'err'); return; }
      Net.chat(v);
      el('inp-chat').value = '';
    }
    el('inp-name').addEventListener('change', function () {
      d.name = this.value.trim().slice(0, 16) || d.name;
      UI.Store.save(); App.updateTopbar();
      Net.send({ t: 'hello', name: d.name, faction: d.faction || 0 });
    });

    // профиль
    el('btn-save-profile').addEventListener('click', function () {
      d.name = el('inp-profile-name').value.trim().slice(0, 16) || d.name;
      d.faction = +el('sel-profile-faction').value;
      d.level = +el('sel-profile-level').value;
      UI.Store.save(); App.updateTopbar(); renderProfile();
      UI.toast('Профиль сохранён');
    });
    el('btn-reset-progress').addEventListener('click', function () {
      UI.modal({
        title: 'Сбросить прогресс?',
        sub: 'Кампания, задачи и статистика вернутся к нулю.',
        actions: [
          { label: 'Отмена', keepOpen: true, onClick: function () { UI.closeModal(); } },
          { label: 'Сбросить', cls: 'pink', onClick: function () { UI.Store.reset(); App.updateTopbar(); renderProfile(); UI.toast('Прогресс сброшен'); } }
        ]
      });
    });
    el('btn-toggle-sound').addEventListener('click', function () {
      d.sound = !d.sound; RZAudio.setSound(d.sound); UI.Store.save(); renderProfile();
    });
    el('btn-toggle-music').addEventListener('click', function () {
      d.music = !d.music; RZAudio.setMusic(d.music); UI.Store.save(); renderProfile();
    });

    // звук
    RZAudio.setSound(d.sound);
    RZAudio.setMusic(d.music);
    const unlock = function () {
      RZAudio.unlock();
      window.removeEventListener('pointerdown', unlock);
      window.removeEventListener('keydown', unlock);
    };
    window.addEventListener('pointerdown', unlock);
    window.addEventListener('keydown', unlock);

    // авто-подключение, если задан сервер
    setTimeout(function () {
      UI.go('menu');
      if (d.server) Net.connect(d.server);
    }, 700);
  };

  window.App = App;
  document.addEventListener('DOMContentLoaded', App.init);
})();
