/* РЕЗОНАНС — «клей» воркера: одинаков для файловой и одностраничной сборки */
(function () {
  'use strict';
  self.onmessage = function (e) {
    const d = e.data;
    if (!d || !d.state) return;
    try {
      const state = RZ.decode(d.state);
      const res = RZ.pickMove(state, { level: d.level, w: d.w, time: d.time });
      self.postMessage({ id: d.id, action: res.action, score: res.score, depth: res.depth, nodes: res.nodes });
    } catch (err) {
      self.postMessage({ id: d.id, error: String(err) });
    }
  };
})();
