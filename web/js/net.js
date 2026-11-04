/* РЕЗОНАНС — сетевой клиент */
(function () {
  'use strict';
  const Net = {
    ws: null, connected: false, me: null, lobby: [], top: [],
    room: null, url: '', reconnectTimer: null, pingTimer: null, lastPing: 0,
    onStatus: null, onLobby: null, onStart: null, onApplied: null,
    onOver: null, onChat: null, onTop: null, onRoom: null, onErr: null, onLeft: null
  };

  Net.defaultUrl = function () {
    const host = location.hostname;
    if ((location.protocol === 'http:' || location.protocol === 'https:') &&
      host && host !== 'localhost' && host !== '127.0.0.1') {
      const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
      return proto + '//' + location.host;
    }
    return 'ws://localhost:8080';
  };

  Net.connect = function (url) {
    if (Net.ws && (Net.ws.readyState === 0 || Net.ws.readyState === 1)) {
      try { Net.ws.close(); } catch (e) { }
    }
    Net.url = url || Net.url || Net.defaultUrl();
    const d = UI.Store.data;
    let ws;
    try { ws = new WebSocket(Net.url); } catch (e) {
      Net.status('Ошибка адреса: ' + Net.url, true); return;
    }
    Net.ws = ws;
    Net.status('Соединение с ' + Net.url + '…');
    ws.onopen = function () {
      Net.connected = true;
      Net.status('В сети · ' + Net.url);
      Net.send({ t: 'hello', name: d.name, faction: d.faction || 0, elo: d.online ? d.online.elo : 1200 });
      Net.pingTimer = setInterval(function () { Net.send({ t: 'ping' }); }, 20000);
    };
    ws.onclose = function () {
      Net.connected = false;
      Net.status('Соединение разорвано', true);
      clearInterval(Net.pingTimer);
    };
    ws.onerror = function () { Net.status('Не удалось подключиться к ' + Net.url, true); };
    ws.onmessage = function (e) {
      let m;
      try { m = JSON.parse(e.data); } catch (err) { return; }
      Net.handle(m);
    };
  };

  Net.send = function (obj) {
    if (!Net.ws || Net.ws.readyState !== 1) return false;
    try { Net.ws.send(JSON.stringify(obj)); return true; } catch (e) { return false; }
  };

  Net.status = function (txt, err) {
    if (Net.onStatus) Net.onStatus(txt, err);
  };

  Net.handle = function (m) {
    switch (m.t) {
      case 'welcome':
        Net.me = m.you;
        Net.lobby = m.lobby || [];
        Net.top = m.top || [];
        if (Net.onTop) Net.onTop(Net.top);
        if (Net.onLobby) Net.onLobby(Net.lobby);
        break;
      case 'lobby':
        Net.lobby = m.rooms || [];
        if (Net.onLobby) Net.onLobby(Net.lobby);
        break;
      case 'room':
        Net.room = m;
        if (Net.onRoom) Net.onRoom(m);
        break;
      case 'left':
        Net.room = null;
        if (Net.onLeft) Net.onLeft();
        break;
      case 'start':
        Net.room = m.room || Net.room;
        if (Net.onStart) Net.onStart(m);
        break;
      case 'applied':
        if (Net.onApplied) Net.onApplied(m);
        break;
      case 'over':
        if (Net.onOver) Net.onOver(m);
        break;
      case 'chat':
        if (Net.onChat) Net.onChat(m);
        break;
      case 'top':
        Net.top = m.list || [];
        if (Net.onTop) Net.onTop(Net.top);
        break;
      case 'sync':
        if (Net.onSync) Net.onSync(m);
        break;
      case 'err':
        Net.status(m.m, true);
        if (Net.onErr) Net.onErr(m.m);
        break;
    }
  };

  Net.createRoom = function (name, faction) {
    const d = UI.Store.data;
    Net.send({ t: 'create', name: d.name, faction: d.faction || 0 });
  };
  Net.join = function (id) { Net.send({ t: 'join', id: id }); };
  Net.quick = function () { Net.send({ t: 'quick' }); };
  Net.leave = function () { Net.send({ t: 'leave' }); Net.room = null; };
  Net.chat = function (msg) { Net.send({ t: 'chat', m: msg }); };
  Net.resign = function () { Net.send({ t: 'resign' }); };
  Net.sendAction = function (a) { Net.send({ t: 'action', a: a }); };

  window.Net = Net;
})();
