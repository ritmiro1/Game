const E = require('../shared/engine.js'), C = require('../shared/content.js'), AI = require('../shared/ai.js');
function play(map, f, lvA, lvB) {
  let s = E.createState({ mapId: map, factions: f });
  let n = 0, kills = null;
  while (!s.over && n++ < 400) {
    const lvl = s.side === 0 ? lvA : lvB;
    const bot = C.BOTS[Math.min(6, lvl)];
    const r = AI.pickMove(s, { level: lvl, w: Object.assign({}, bot.w), time: 30 + lvl * 15 });
    E.applyInPlace(s, r.action);
  }
  // did a core die?
  kills = E.unitCount(s, 0) > 0 && E.unitCount(s, 1) > 0 ? 'points' : 'core';
  return { winner: s.winner, turns: s.turn, kills };
}
const t0 = Date.now(); let out = { core: 0, points: 0 }; let w = [0, 0, 0];
for (let g = 0; g < 10; g++) {
  const r = play(C.MAPS[g % C.MAPS.length].id, [g % 3, (g + 1) % 3], 3, 3);
  out[r.kills]++; w[r.winner === -1 ? 2 : r.winner]++;
  console.log(r);
}
console.log(out, 'wins', w, 'ms', Date.now() - t0);
