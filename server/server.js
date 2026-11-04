/* РЕЗОНАНС — сетевой сервер: статика + WebSocket (лобби, дуэли, рейтинг, чат) */
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');
const E = require('../shared/engine.js');
const C = require('../shared/content.js');

const PORT = process.env.PORT ? parseInt(process.env.PORT, 10) : 8080;
const ROOT = path.join(__dirname, '..');
const DATA_FILE = path.join(__dirname, 'data.json');

/* ---------------- хранилище ---------------- */
let DB = { players: {}, history: [] };
try {
  if (fs.existsSync(DATA_FILE)) DB = Object.assign(DB, JSON.parse(fs.readFileSync(DATA_FILE, 'utf8')));
} catch (e) { console.warn('[db] не удалось прочитать data.json:', e.message); }
let saveTimer = null;
function saveDB() {
  if (saveTimer) return;
  saveTimer = setTimeout(() => {
    saveTimer = null;
    try { fs.writeFileSync(DATA_FILE, JSON.stringify(DB, null, 1)); }
    catch (e) { console.warn('[db] запись не удалась:', e.message); }
  }, 1200);
}
function player(name) {
  const key = String(name || '').toLowerCase().slice(0, 24) || 'безымянный';
  if (!DB.players[key]) DB.players[key] = { name: String(name).slice(0, 16), elo: 1200, wins: 0, games: 0, lastSeen: Date.now() };
  DB.players[key].lastSeen = Date.now();
  return DB.players[key];
}
function topList(limit) {
  return Object.values(DB.players)
    .filter(p => p.games > 0)
    .sort((a, b) => b.elo - a.elo)
    .slice(0, limit || 20)
    .map(p => ({ name: p.name, elo: p.elo, games: p.games, wins: p.wins }));
}
function eloUpdate(ra, rb, scoreA) {
  const K = 24;
  const ea = 1 / (1 + Math.pow(10, (rb - ra) / 400));
  return Math.round(ra + K * (scoreA - ea));
}

/* ---------------- статика ---------------- */
const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8',
  '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml', '.ico': 'image/x-icon',
  '.woff2': 'font/woff2', '.txt': 'text/plain; charset=utf-8', '.exe': 'application/octet-stream'
};
const server = http.createServer((req, res) => {
  let url = decodeURIComponent((req.url || '/').split('?')[0]);
  if (url === '/' || url === '') { res.writeHead(302, { Location: '/web/index.html' }); return res.end(); }
  if (url === '/health') { res.writeHead(200, { 'Content-Type': 'text/plain' }); return res.end('ok'); }
  if (url.indexOf('..') >= 0) { res.writeHead(400); return res.end('bad'); }
  const file = path.join(ROOT, url);
  if (!file.startsWith(ROOT) || /(^|[\\/])(\.git|node_modules|server)([\\/]|$)/.test(url)) {
    res.writeHead(403); return res.end('forbidden');
  }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }); return res.end('404'); }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file).toLowerCase()] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
    res.end(data);
  });
});

/* ---------------- состояние сети ---------------- */
const wss = new WebSocketServer({ server });
const clients = new Map(); // ws -> client
const rooms = new Map();   // id -> room
let queue = [];            // ожидающие быстрого подбора
let nextRoom = 1;
let nextClient = 1;

function send(ws, obj) {
  if (ws && ws.readyState === 1) {
    try { ws.send(JSON.stringify(obj)); } catch (e) { }
  }
}
function broadcastRoom(room, obj, except) {
  if (room.host && room.host.ws !== except) send(room.host.ws, obj);
  if (room.guest && room.guest.ws !== except) send(room.guest.ws, obj);
}
function lobbyList() {
  const out = [];
  rooms.forEach(r => {
    const host = r.host;
    if (!host) return;
    out.push({
      id: r.id, name: host.name, faction: host.faction, elo: host.elo,
      map: (C.MAP_BY_ID[r.mapId] || {}).name || '—',
      status: r.status, players: (r.host ? 1 : 0) + (r.guest ? 1 : 0)
    });
  });
  return out.slice(0, 40);
}
function broadcastLobby() {
  const list = lobbyList();
  clients.forEach(c => send(c.ws, { t: 'lobby', rooms: list }));
}
function chatBroadcast(name, text, sys) {
  clients.forEach(c => send(c.ws, { t: 'chat', name: name, m: text, sys: !!sys }));
}

/* ---------------- комнаты ---------------- */
function randomMap() {
  const maps = C.MAPS;
  return maps[Math.floor(Math.random() * maps.length)].id;
}
function createRoom(client, opts) {
  opts = opts || {};
  const id = 'r' + (nextRoom++);
  const room = {
    id: id, host: client, guest: null, status: 'waiting',
    mapId: opts.mapId || randomMap(), state: null, startedAt: 0, actions: []
  };
  client.room = room; client.side = 0;
  rooms.set(id, room);
  broadcastLobby();
  send(client.ws, { t: 'room', id: id, name: client.name, map: (C.MAP_BY_ID[room.mapId] || {}).name });
  return room;
}

function startRoom(room) {
  room.status = 'playing';
  room.startedAt = Date.now();
  room.state = E.createState({ mapId: room.mapId, factions: [room.host.faction, room.guest.faction] });
  const players = [
    { name: room.host.name, elo: room.host.elo, faction: room.host.faction },
    { name: room.guest.name, elo: room.guest.elo, faction: room.guest.faction }
  ];
  const enc = E.encode(room.state);
  send(room.host.ws, { t: 'start', state: enc, side: 0, players: players, room: { id: room.id, map: room.mapId } });
  send(room.guest.ws, { t: 'start', state: enc, side: 1, players: players, room: { id: room.id, map: room.mapId } });
  chatBroadcast('', '⚔ Дуэль: ' + room.host.name + ' против ' + room.guest.name + ' на арене «' + (C.MAP_BY_ID[room.mapId] || {}).name + '»', true);
  broadcastLobby();
}

function leaveRoom(client, silent) {
  const room = client.room;
  if (!room) return;
  if (room.status === 'playing') {
    const other = room.host === client ? room.guest : room.host;
    if (other) {
      endGame(room, client.side === 0 ? 1 : 0, 'отключение');
      return;
    }
  }
  rooms.delete(room.id);
  if (room.host === client) room.host = null;
  if (room.guest === client) room.guest = null;
  client.room = null;
  client.side = -1;
  if (!silent) send(client.ws, { t: 'left' });
  broadcastLobby();
}

function endGame(room, winner, reason) {
  if (room.status === 'over') return;
  room.status = 'over';
  const h = room.host, g = room.guest;
  let eloH = h ? h.elo : 1200, eloG = g ? g.elo : 1200;
  if (h && g && h.name.toLowerCase() !== g.name.toLowerCase()) {
    const scoreH = winner === 0 ? 1 : winner === 1 ? 0 : 0.5;
    const newH = eloUpdate(h.elo, g.elo, scoreH);
    const newG = eloUpdate(g.elo, h.elo, 1 - scoreH);
    eloH = newH; eloG = newG;
    const ph = player(h.name), pg = player(g.name);
    ph.elo = newH; ph.games++; if (winner === 0) ph.wins++;
    pg.elo = newG; pg.games++; if (winner === 1) pg.wins++;
    h.elo = newH; g.elo = newG;
    DB.history.unshift({
      d: Date.now(), map: room.mapId,
      p: [{ n: h.name, e: newH }, { n: g.name, e: newG }], w: winner, r: reason, turns: room.state ? room.state.turn : 0
    });
    DB.history = DB.history.slice(0, 80);
    saveDB();
  }
  const enc = room.state ? E.encode(room.state) : null;
  const top = topList(20);
  if (h) send(h.ws, { t: 'over', winner: winner, reason: reason, elo: eloH, state: enc, top: top });
  if (g) send(g.ws, { t: 'over', winner: winner, reason: reason, elo: eloG, state: enc, top: top });
  clients.forEach(c => send(c.ws, { t: 'top', list: top }));
  if (h) { h.room = null; h.side = -1; }
  if (g) { g.room = null; g.side = -1; }
  rooms.delete(room.id);
  broadcastLobby();
  chatBroadcast('', '🏁 Победа: ' + (winner === 0 ? (h && h.name) : winner === 1 ? (g && g.name) : 'ничья') + ' (' + reason + ')', true);
}

/* ---------------- сокеты ---------------- */
wss.on('connection', (ws) => {
  const client = { id: nextClient++, ws: ws, name: 'Гость', elo: 1200, faction: 0, room: null, side: -1 };
  clients.set(ws, client);
  send(ws, { t: 'welcome', you: { id: client.id, name: client.name, elo: client.elo }, lobby: lobbyList(), top: topList(20) });

  ws.on('message', (raw) => {
    let m;
    try { m = JSON.parse(raw.toString()); } catch (e) { return; }
    if (!m || !m.t) return;

    switch (m.t) {
      case 'hello': {
        client.name = String(m.name || 'Гость').slice(0, 16) || 'Гость';
        client.faction = Math.max(0, Math.min(C.FACTIONS.length - 1, m.faction | 0));
        const p = player(client.name);
        p.name = client.name;
        client.elo = p.elo;
        client.profile = p;
        send(ws, { t: 'welcome', you: { id: client.id, name: client.name, elo: client.elo, wins: p.wins, games: p.games }, lobby: lobbyList(), top: topList(20) });
        chatBroadcast('', client.name + ' вошёл в контур', true);
        broadcastLobby();
        break;
      }
      case 'faction':
        client.faction = Math.max(0, Math.min(C.FACTIONS.length - 1, m.faction | 0));
        break;
      case 'lobby':
        send(ws, { t: 'lobby', rooms: lobbyList() });
        break;
      case 'create': {
        if (client.room) leaveRoom(client, true);
        const room = createRoom(client, m);
        chatBroadcast('', client.name + ' открыл дуэль на арене «' + (C.MAP_BY_ID[room.mapId] || {}).name + '»', true);
        break;
      }
      case 'join': {
        const room = rooms.get(m.id);
        if (!room) return send(ws, { t: 'err', m: 'Дуэль уже недоступна' });
        if (room.status !== 'waiting') return send(ws, { t: 'err', m: 'В этой дуэли уже играют' });
        if (room.host === client) return send(ws, { t: 'err', m: 'Это твоя дуэль' });
        if (client.room) leaveRoom(client, true);
        queue = queue.filter(c => c !== client);
        room.guest = client;
        client.room = room; client.side = 1;
        startRoom(room);
        break;
      }
      case 'quick': {
        if (client.room) return send(ws, { t: 'err', m: 'Ты уже в дуэли' });
        const waiting = rooms.get(client.quickRoom);
        if (waiting && waiting.status === 'waiting' && waiting.host === client) return;
        if (queue.indexOf(client) < 0) queue.push(client);
        const others = queue.filter(c => c !== client && c.ws.readyState === 1);
        if (others.length) {
          const other = others[0];
          queue = queue.filter(c => c !== client && c !== other);
          const room = createRoom(other, {});
          room.guest = client; client.room = room; client.side = 1;
          startRoom(room);
        } else {
          send(ws, { t: 'err', m: 'Ждём соперника… (открой игру во второй вкладке, чтобы проверить)' });
        }
        break;
      }
      case 'leave':
        queue = queue.filter(c => c !== client);
        if (client.room) leaveRoom(client);
        else send(ws, { t: 'left' });
        broadcastLobby();
        break;
      case 'action': {
        const room = client.room;
        if (!room || room.status !== 'playing' || !room.state) return send(ws, { t: 'err', m: 'Дуэль не идёт' });
        if (room.state.side !== client.side) return send(ws, { t: 'err', m: 'Сейчас не твой ход' });
        const a = m.a;
        if (!a || !E.isLegal(room.state, a)) {
          send(ws, { t: 'sync', state: E.encode(room.state) });
          return send(ws, { t: 'err', m: 'Недопустимое действие' });
        }
        E.applyInPlace(room.state, a);
        room.actions.push(a);
        const other = room.host === client ? room.guest : room.host;
        if (other) send(other.ws, { t: 'applied', side: client.side, a: a, state: E.encode(room.state) });
        if (room.state.over) {
          endGame(room, room.state.winner, room.state.winner === -1 ? 'контроль' : 'контур');
        }
        break;
      }
      case 'resign': {
        const room = client.room;
        if (!room || room.status !== 'playing') return;
        endGame(room, client.side === 0 ? 1 : 0, 'сдался');
        break;
      }
      case 'chat':
        if (m.m) chatBroadcast(client.name, String(m.m).slice(0, 180), false);
        break;
      case 'ping':
        break;
    }
  });

  ws.on('close', () => {
    queue = queue.filter(c => c !== client);
    if (client.room) leaveRoom(client, true);
    clients.delete(ws);
    broadcastLobby();
  });
  ws.on('error', () => { });
});

/* уборка комнат-призраков */
setInterval(() => {
  const now = Date.now();
  rooms.forEach(r => {
    if (r.status === 'waiting' && (!r.host || r.host.ws.readyState !== 1)) rooms.delete(r.id);
  });
  queue = queue.filter(c => c.ws.readyState === 1);
  broadcastLobby();
}, 30000);

server.listen(PORT, '0.0.0.0', () => {
  console.log('РЕЗОНАНС · сервер слушает http://0.0.0.0:' + PORT);
  console.log('Открой /web/index.html или http://localhost:' + PORT + '/');
});
