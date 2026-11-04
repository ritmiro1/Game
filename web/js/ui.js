/* РЕЗОНАНС — DOM-утилиты, модалки, тосты, хранилище профиля */
(function () {
  'use strict';
  const UI = {};

  /* ---------- DOM ---------- */
  UI.$ = function (sel, root) { return (root || document).querySelector(sel); };
  UI.$$ = function (sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); };
  UI.el = function (tag, cls, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html !== undefined) e.innerHTML = html;
    return e;
  };
  UI.esc = function (s) {
    return String(s === undefined || s === null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  };

  /* ---------- экраны ---------- */
  UI.current = 'loading';
  UI.go = function (name) {
    const el = document.getElementById('s-' + name);
    if (!el) return;
    UI.$$('.screen').forEach(function (s) { s.classList.remove('active'); });
    el.classList.add('active');
    el.scrollTop = 0;
    UI.current = name;
    if (window.App && App.onScreen) App.onScreen(name);
  };

  /* ---------- тосты ---------- */
  UI.toast = function (msg, type) {
    const root = document.getElementById('toasts');
    const t = UI.el('div', 'toast' + (type === 'err' ? ' err' : ''), UI.esc(msg));
    root.appendChild(t);
    setTimeout(function () {
      t.style.transition = 'opacity .3s, transform .3s';
      t.style.opacity = '0'; t.style.transform = 'translateY(10px)';
      setTimeout(function () { t.remove(); }, 320);
    }, 2600);
  };

  /* ---------- модалки ---------- */
  let modalOnClose = null;
  UI.modal = function (opts) {
    const root = document.getElementById('modal-root');
    const box = UI.el('div', 'modal');
    let html = '';
    if (opts.title) html += '<h2>' + opts.title + '</h2>';
    if (opts.sub) html += '<div class="sub">' + opts.sub + '</div>';
    html += '<div class="modal-body">' + (opts.body || '') + '</div>';
    if (opts.actions && opts.actions.length) {
      html += '<div class="modal-actions">';
      opts.actions.forEach(function (a, i) {
        html += '<button class="btn ' + (a.cls || '') + '" data-mi="' + i + '">' + a.label + '</button>';
      });
      html += '</div>';
    }
    box.innerHTML = html;
    root.innerHTML = '';
    root.appendChild(box);
    root.classList.add('on');
    modalOnClose = opts.onClose || null;
    UI.$$('[data-mi]', box).forEach(function (b) {
      b.addEventListener('click', function () {
        const a = opts.actions[+b.getAttribute('data-mi')];
        if (a.keepOpen) { if (a.onClick) a.onClick(box); }
        else { UI.closeModal(); if (a.onClick) a.onClick(); }
      });
    });
    return box;
  };
  UI.closeModal = function () {
    const root = document.getElementById('modal-root');
    if (!root.classList.contains('on')) return;
    root.classList.remove('on');
    root.innerHTML = '';
    const cb = modalOnClose; modalOnClose = null;
    if (cb) cb();
  };
  UI.modalOpen = function () { return document.getElementById('modal-root').classList.contains('on'); };

  /* ---------- хранилище ---------- */
  const KEY = 'rezonans.profile.v1';
  const DEFAULT = {
    name: '', faction: 0, level: 3, sound: true, music: true,
    elo: 1200, wins: 0, games: 0, streak: 0, best: 0,
    campaign: {}, puzzles: {}, server: '', aiName: 'Гость',
    online: { elo: 1200, wins: 0, games: 0 }
  };
  const Store = {
    data: JSON.parse(JSON.stringify(DEFAULT)),
    load: function () {
      try {
        const raw = localStorage.getItem(KEY);
        if (raw) {
          const o = JSON.parse(raw);
          Object.assign(this.data, o);
          this.data.online = Object.assign({ elo: 1200, wins: 0, games: 0 }, o.online || {});
        }
      } catch (e) { }
      if (!this.data.name) this.data.name = 'Ткач-' + Math.floor(1000 + Math.random() * 9000);
      return this.data;
    },
    save: function () {
      try { localStorage.setItem(KEY, JSON.stringify(this.data)); } catch (e) { }
    },
    reset: function () {
      this.data = JSON.parse(JSON.stringify(DEFAULT));
      this.data.name = 'Ткач-' + Math.floor(1000 + Math.random() * 9000);
      this.save();
    }
  };

  UI.Store = Store;
  UI.fmt = function (n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ' '); };
  UI.plural = function (n, one, few, many) {
    const m = Math.abs(n) % 100, d = m % 10;
    if (m > 10 && m < 20) return many;
    if (d === 1) return one;
    if (d > 1 && d < 5) return few;
    return many;
  };

  window.UI = UI;
})();
