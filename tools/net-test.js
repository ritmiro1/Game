/* Проверка сетевого протокола: два клиента, полная дуэль через сервер */
const WebSocket = require('ws');
const E = require('../shared/engine.js');

const URL = 'ws://127.0.0.1:8080';
const log = (...a) => console.log(...a);
let fails = 0;
function fail(m) { fails++; console.log('✗', m); }

function mk(name, faction) {
  const ws = new WebSocket(URL);
  const c = { ws, name, faction, state: null, side: -1, msgs: [] };
  ws.on('message', (raw) => {
    const m = JSON.parse(raw.toString());
    c.msgs.push(m);
    if (m.t === 'start') { c.state = E.decode(m.state); c.side = m.side; }
    if (m.t === 'applied') { c.state = E.decode(m.state); }
    if (m.t === 'over') { c.over = m; }
    if (m.t === 'sync') { c.state = E.decode(m.state); }
  });
  ws.on('open', () => ws.send(JSON.stringify({ t: 'hello', name, faction })));
  return c;
}
const wait = ms => new Promise(r => setTimeout(r, ms));

(async () => {
  const a = mk('Тест-Альфа', 0);
  const b = mk('Тест-Бета', 1);
  await wait(400);
  if (!a.msgs.find(m => m.t === 'welcome')) fail('нет welcome у A');

  a.ws.send(JSON.stringify({ t: 'create' }));
  await wait(300);
  const room = a.msgs.find(m => m.t === 'room');
  if (!room) fail('комнаты нет');
  b.ws.send(JSON.stringify({ t: 'join', id: room.id }));
  await wait(400);
  if (!a.state || !b.state) fail('дуэль не началась');
  log('дуэль началась, арена', room.map, 'стороны', a.side, b.side);

  // попытка сходить не в свой ход
  b.ws.send(JSON.stringify({ t: 'action', a: { t: 'end' } }));
  await wait(150);
  if (!b.msgs.find(m => m.t === 'err')) fail('сервер не отклонил ход не в свою очередь');

  let sim = E.decode(a.msgs.find(m => m.t === 'start').state);
  let steps = 0;
  while (steps++ < 500 && !sim.over) {
    const mover = sim.side === a.side ? a : b;
    const mv = E.getMoves(sim);
    const act = mv[Math.floor(Math.random() * mv.length)];
    mover.ws.send(JSON.stringify({ t: 'action', a: act }));
    E.applyInPlace(sim, act);
    await wait(6);
  }
  log('шагов', steps, 'ходов', sim.turn, 'победитель', sim.winner);
  await wait(500);
  if (!a.over || !b.over) fail('нет события over');
  else log('победа стороны', a.over.winner, '| elo', a.over.elo, b.over.elo, '| top', (a.over.top || []).length);
  if (a.over && b.over && a.over.elo !== undefined) log('elo A', a.over.elo, 'elo B', b.over.elo);

  // чат
  a.ws.send(JSON.stringify({ t: 'chat', m: 'привет' }));
  await wait(200);
  if (!b.msgs.find(m => m.t === 'chat' && m.m === 'привет')) fail('чат не дошёл');

  // быстрый подбор
  const c = mk('Тест-Гамма', 2), d = mk('Тест-Дельта', 0);
  await wait(300);
  c.ws.send(JSON.stringify({ t: 'quick' }));
  await wait(120);
  d.ws.send(JSON.stringify({ t: 'quick' }));
  await wait(500);
  if (!c.state || !d.state) fail('быстрый подбор не сработал');
  else log('быстрый подбор: дуэль', c.state.mapId, 'стороны', c.side, d.side);

  [a, b, c, d].forEach(x => x.ws.close());
  await wait(200);
  log(fails ? 'ОШИБОК: ' + fails : '✓ сеть в порядке');
  process.exit(fails ? 1 : 0);
})();
