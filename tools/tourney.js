const E = require('../shared/engine.js'), C = require('../shared/content.js'), AI = require('../shared/ai.js');
function play(map, f, lvA, lvB, seed) {
  let s = E.createState({ mapId: map, factions: f });
  let n = 0;
  while (!s.over && n++ < 400) {
    const lvl = s.side === 0 ? lvA : lvB;
    const bot = C.BOTS[Math.min(6, lvl)];
    const r = AI.pickMove(s, { level: lvl, w: Object.assign({}, bot.w), time: 60 + lvl * 20 });
    if (!E.isLegal(s, r.action)) { console.log('illegal', r.action); break; }
    E.applyInPlace(s, r.action);
  }
  return { winner: s.winner, turns: s.turn, score: [E.score(s,0).toFixed(1), E.score(s,1).toFixed(1)] };
}
const t0 = Date.now();
let wins = { strong: 0, weak: 0, draw: 0 };
for (let g = 0; g < 8; g++) {
  const map = C.MAPS[g % C.MAPS.length].id;
  const strongSide = g % 2;
  const r = strongSide === 0 ? play(map, [0, 1], 4, 1) : play(map, [1, 0], 1, 4);
  const winner = r.winner;
  if (winner === -1) wins.draw++;
  else if (winner === strongSide) wins.strong++;
  else wins.weak++;
  console.log(map, 'strongSide', strongSide, 'winner', winner, 'turns', r.turns, 'score', r.score.join(':'));
}
console.log(wins, 'ms', Date.now() - t0);
