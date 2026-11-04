const E = require('../shared/engine.js');
const C = require('../shared/content.js');
const AI = require('../shared/ai.js');

function ascii(s) {
  const b = s.board;
  let out = '';
  for (let r = -b.R; r <= b.R; r++) {
    let line = ' '.repeat(Math.abs(r) * 2);
    for (let q = -b.R; q <= b.R; q++) {
      const i = b.idx[q + ',' + r];
      if (i === undefined) continue;
      const o = s.occ[i];
      let ch = '.';
      if (b.blocked[i]) ch = '#';
      else if (s.barrier[i] > 0) ch = '=';
      else if (o > 0) { const u = o - 1; const t = C.TYPES[s.uType[u]]; ch = s.uOwner[u] === 0 ? t.glyph : t.glyph; if (s.uType[u] === 0) ch = s.uOwner[u]===0?'C':'c'; }
      else if (b.node[i]) ch = 'o';
      else if (i === b.base[0] || i === b.base[1]) ch = 'B';
      line += ch + ' ';
    }
    out += line + '\n';
  }
  return out;
}
let s = E.createState({ mapId: 'rift', factions: [0, 1] });
console.log(ascii(s));
console.log('moves:', E.getMoves(s).length, 'energy', s.energy[0], s.energy[1]);

// random playouts
let t0 = Date.now(), games = 60, results = {0:0,1:0,'-1':0}, plies = 0, bad = 0;
for (let g = 0; g < games; g++) {
  let st = E.createState({ mapId: C.MAPS[g % C.MAPS.length].id, factions: [g % 3, (g + 1) % 3] });
  let guard = 0;
  while (!st.over && guard++ < 500) {
    const mv = E.getMoves(st);
    if (!mv.length) { bad++; break; }
    const a = mv[Math.floor(Math.random() * mv.length)];
    if (!E.isLegal(st, a)) { console.log('ILLEGAL', JSON.stringify(a)); bad++; break; }
    E.applyInPlace(st, a);
    plies++;
  }
  if (!st.over) bad++;
  results[st.winner] = (results[st.winner] || 0) + 1;
}
console.log('random playouts:', results, 'bad:', bad, 'plies:', plies, 'ms:', Date.now() - t0);

// encode/decode roundtrip
let st2 = E.createState({ mapId: 'dust', factions: [2, 1] });
for (let i = 0; i < 7; i++) { const mv = E.getMoves(st2); E.applyInPlace(st2, mv[0]); }
const enc = E.encode(st2), dec = E.decode(enc);
console.log('roundtrip ok:', JSON.stringify(E.encode(dec)) === JSON.stringify(enc));

// AI timing
for (const lvl of [1,3,5,6,7]) {
  let st = E.createState({ mapId: 'clover', factions: [0, 1] });
  for (let i = 0; i < 6; i++) { const r = AI.pickMove(st, {level:3}); E.applyInPlace(st, r.action); }
  t0 = Date.now();
  const r = AI.pickMove(st, { level: lvl });
  console.log('level', lvl, 'depth', r.depth, 'nodes', r.nodes, 'ms', Date.now() - t0, 'score', r.score);
}
