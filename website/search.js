/* Site search. A button in the header (.search-btn) opens a dialog; the index (search-index.json, built by tools/website/gen_search.py)
   loads the first time it is needed. Every blog post, Library entry, embed, page, code sample and PDF page is in it.
   Keys: Cmd/Ctrl+K or / opens, arrows move, Enter opens, Esc closes. */
(function () {
  'use strict';
  var script = document.currentScript || document.querySelector('script[src*="search.js"]');
  var ROOT = script ? new URL('.', script.src).href : new URL('./', location.href).href;
  var VER = script && /v=([^&]+)/.exec(script.src) ? RegExp.$1 : '1';
  var KINDS = ['All', 'Blog', 'Library', 'Embed', 'Doc', 'Code', 'Page'];
  var KIND_LABEL = { All: 'All', Blog: 'Blog', Library: 'Library', Embed: 'Embeds', Doc: 'PDFs', Code: 'Code', Page: 'Pages' };
  var ICON = {
    Blog: '<path d="M4 20h4L19 9a2.1 2.1 0 0 0-3-3L5 17v3Z"/>',
    Library: '<rect x="4" y="4" width="4" height="16" rx="1.2"/><rect x="10" y="4" width="4" height="16" rx="1.2"/><path d="m16 5.5 3-.8 3 13.3-3 .8Z"/>',
    Embed: '<path d="m8 8-4 4 4 4M16 8l4 4-4 4M13.5 5l-3 14"/>',
    Doc: '<path d="M6 3h8l4 4v14H6Z"/><path d="M14 3v4h4M9 12h6M9 16h6"/>',
    Code: '<path d="m9 8-4 4 4 4M15 8l4 4-4 4"/>',
    Page: '<rect x="4" y="4" width="16" height="16" rx="3"/><path d="M4 9h16"/>'
  };
  var NS = 'http://www.w3.org/2000/svg';
  var data = null, loading = null, dlg = null, input, list, chips, kind = 'All', sel = 0, results = [], last = null;

  function svg(inner) {
    var s = document.createElementNS(NS, 'svg');
    s.setAttribute('viewBox', '0 0 24 24'); s.setAttribute('fill', 'none'); s.setAttribute('stroke', 'currentColor');
    s.setAttribute('stroke-width', '1.8'); s.setAttribute('stroke-linecap', 'round'); s.setAttribute('stroke-linejoin', 'round'); s.setAttribute('aria-hidden', 'true');
    s.innerHTML = inner; return s;   // inner is a constant from ICON above, never page data
  }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

  function load() {
    if (data) return Promise.resolve(data);
    if (!loading) loading = fetch(ROOT + 'search-index.json?v=' + VER).then(function (r) { return r.json(); }).then(function (j) {
      data = j.i.map(function (e) {
        return { t: e[0], u: e[1], k: e[2], d: e[3], x: e[4], s: e[5] || [], lt: e[0].toLowerCase(), ld: (e[3] || '').toLowerCase(), lx: (e[4] || '').toLowerCase() };
      });
      return data;
    }).catch(function () { loading = null; return []; });
    return loading;
  }

  function tokens(q) { return q.toLowerCase().split(/[^a-z0-9#+._-]+/).filter(Boolean); }
  function wordAt(s, w) { var i = s.indexOf(w); while (i > 0 && /[a-z0-9]/.test(s[i - 1])) i = s.indexOf(w, i + 1); return i; }

  function score(e, ts, whole) {
    var sc = 0;
    for (var i = 0; i < ts.length; i++) {
      var w = ts[i], s = 0, p;
      if ((p = wordAt(e.lt, w)) >= 0) s = p === 0 ? 120 : 90;
      else if (e.lt.indexOf(w) >= 0) s = 55;
      else if (e.ld.indexOf(w) >= 0) s = 38;
      else if (e.lx.indexOf(w) >= 0) s = (wordAt(e.lx, w) >= 0 ? 16 : 9);
      else return 0;
      sc += s;
    }
    if (ts.length > 1 && e.lt.indexOf(whole) >= 0) sc += 80;
    else if (ts.length > 1 && (e.ld.indexOf(whole) >= 0 || e.lx.indexOf(whole) >= 0)) sc += 30;
    if (e.k === 'Page') sc += 6;
    return sc;
  }

  function run(q) {
    var ts = tokens(q), out = [];
    if (!ts.length) return out;
    var whole = ts.join(' ');
    for (var i = 0; i < data.length; i++) {
      var s = score(data[i], ts, whole);
      if (s) out.push({ e: data[i], s: s });
    }
    out.sort(function (a, b) { return b.s - a.s; });
    return out;
  }

  function snippet(e, ts) {
    var src = e.k === 'Code' ? e.x : (e.x || e.d), low = src.toLowerCase(), at = -1, i;
    if (e.k !== 'Code' && e.d) for (i = 0; i < ts.length; i++) if (e.ld.indexOf(ts[i]) >= 0) { src = e.d; low = e.ld; break; }
    for (i = 0; i < ts.length && at < 0; i++) at = low.indexOf(ts[i]);
    if (at < 0) return e.d || src.slice(0, 140);
    var a = Math.max(0, at - 50), b = Math.min(src.length, at + 130);
    return (a ? '…' : '') + src.slice(a, b) + (b < src.length ? '…' : '');
  }

  function mark(node, text, ts) {
    var low = text.toLowerCase(), pos = 0, hits = [];
    ts.forEach(function (w) { var i = wordAt(low, w); if (i < 0) i = low.indexOf(w); if (i >= 0 && (w.length > 1 || wordAt(low, w) === i)) hits.push([i, i + w.length]); });
    hits.sort(function (a, b) { return a[0] - b[0]; });
    hits.forEach(function (h) {
      if (h[0] < pos) return;
      node.appendChild(document.createTextNode(text.slice(pos, h[0])));
      var m = document.createElement('mark'); m.textContent = text.slice(h[0], h[1]); node.appendChild(m); pos = h[1];
    });
    node.appendChild(document.createTextNode(text.slice(pos)));
  }

  function href(e) { return ROOT + e.u; }

  function renderChips(counts) {
    chips.textContent = '';
    KINDS.forEach(function (k) {
      var b = el('button', 'sr-chip', KIND_LABEL[k]); b.type = 'button'; b.setAttribute('aria-pressed', String(k === kind));
      if (counts && counts[k] != null && k !== 'All') { var sm = el('small', null, String(counts[k])); b.appendChild(sm); }
      b.addEventListener('click', function () { kind = k; update(); input.focus(); });
      chips.appendChild(b);
    });
  }

  function item(e, ts, i) {
    var li = el('li', 'sr-item'); li.id = 'sr-o' + i; li.setAttribute('role', 'option'); li.setAttribute('aria-selected', String(i === sel)); li.dataset.kind = e.k;
    var a = el('a'); a.href = href(e);
    var ic = el('span', 'sr-ico'); ic.appendChild(svg(ICON[e.k] || ICON.Page));
    var t = el('div', 'sr-t'); mark(t, e.t, ts); t.appendChild(el('span', 'sr-k', KIND_LABEL[e.k].replace(/s$/, '')));
    var s = el('div', 'sr-s'); mark(s, snippet(e, ts), ts);
    a.appendChild(ic); a.appendChild(t); a.appendChild(s); li.appendChild(a);
    li.addEventListener('mousemove', function () { if (sel !== i) { sel = i; paintSel(false); } });
    a.addEventListener('click', close);
    return li;
  }

  function paintSel(scroll) {
    var items = list.children;
    for (var i = 0; i < items.length; i++) items[i].setAttribute('aria-selected', String(i === sel));
    if (items[sel]) { input.setAttribute('aria-activedescendant', items[sel].id); if (scroll) items[sel].scrollIntoView({ block: 'nearest' }); }
  }

  var SUGGEST = ['wavetable', 'reverb', 'audio reactive', 'headless cli', 'field language', 'vst3', 'midi', 'shader'];
  function update() {
    var q = input.value.trim(), ts = tokens(q);
    list.textContent = '';
    if (!data) { list.appendChild(el('li', 'sr-empty', 'Loading the index…')); renderChips(); return; }
    if (!ts.length) {
      renderChips();
      var box = el('li', 'sr-empty', 'Search every page, blog post, Library entry, embed, code sample and PDF.');
      var row = el('div'); row.style.cssText = 'display:flex;flex-wrap:wrap;gap:6px;justify-content:center;margin-top:14px';
      SUGGEST.forEach(function (w) { var b = el('button', 'sr-chip', w); b.type = 'button'; b.addEventListener('click', function () { input.value = w; update(); input.focus(); }); row.appendChild(b); });
      box.appendChild(row); list.appendChild(box); results = []; return;
    }
    var all = run(q), counts = { All: all.length };
    all.forEach(function (r) { counts[r.e.k] = (counts[r.e.k] || 0) + 1; });
    renderChips(counts);
    results = (kind === 'All' ? all : all.filter(function (r) { return r.e.k === kind; })).slice(0, 40);
    sel = 0;
    if (!results.length) { list.appendChild(el('li', 'sr-empty', 'Nothing found for “' + q + '”. Try fewer words.')); return; }
    results.forEach(function (r, i) { list.appendChild(item(r.e, ts, i)); });
    paintSel(false);
  }

  function build() {
    dlg = el('div', 'sr'); dlg.setAttribute('role', 'dialog'); dlg.setAttribute('aria-modal', 'true'); dlg.setAttribute('aria-label', 'Search Infinite');
    var scrim = el('div', 'sr-scrim'); scrim.addEventListener('click', close);
    var box = el('div', 'sr-box');
    var f = el('div', 'sr-field'); f.appendChild(svg('<circle cx="11" cy="11" r="6.5"/><path d="m16 16 4.5 4.5"/>'));
    input = el('input', 'sr-input'); input.type = 'search'; input.placeholder = 'Search Infinite'; input.autocomplete = 'off'; input.spellcheck = false;
    input.setAttribute('aria-label', 'Search'); input.setAttribute('role', 'combobox'); input.setAttribute('aria-controls', 'sr-list'); input.setAttribute('aria-expanded', 'true');
    var esc = el('button', 'sr-esc', 'esc'); esc.type = 'button'; esc.setAttribute('aria-label', 'Close search'); esc.addEventListener('click', close);
    f.appendChild(input); f.appendChild(esc);
    chips = el('div', 'sr-chips'); chips.setAttribute('role', 'group'); chips.setAttribute('aria-label', 'Filter by type');
    list = el('ul', 'sr-list'); list.id = 'sr-list'; list.setAttribute('role', 'listbox');
    var hint = el('div', 'sr-hint'); hint.innerHTML = '<span><kbd>↑</kbd> <kbd>↓</kbd> move</span><span><kbd>↵</kbd> open</span><span><kbd>esc</kbd> close</span>';
    box.appendChild(f); box.appendChild(chips); box.appendChild(list); box.appendChild(hint);
    dlg.appendChild(scrim); dlg.appendChild(box); document.body.appendChild(dlg);
    input.addEventListener('input', update);
    input.addEventListener('keydown', function (ev) {
      if (ev.key === 'ArrowDown') { ev.preventDefault(); if (results.length) { sel = (sel + 1) % results.length; paintSel(true); } }
      else if (ev.key === 'ArrowUp') { ev.preventDefault(); if (results.length) { sel = (sel - 1 + results.length) % results.length; paintSel(true); } }
      else if (ev.key === 'Enter') { var r = results[sel]; if (r) { ev.preventDefault(); close(); location.href = href(r.e); } }
    });
    dlg.addEventListener('keydown', function (ev) {
      if (ev.key === 'Escape') { ev.preventDefault(); close(); }
      else if (ev.key === 'Tab') {   // keep focus inside the dialog
        var f2 = dlg.querySelectorAll('input, button, a'); if (!f2.length) return;
        var first = f2[0], lastEl = f2[f2.length - 1];
        if (ev.shiftKey && document.activeElement === first) { ev.preventDefault(); lastEl.focus(); }
        else if (!ev.shiftKey && document.activeElement === lastEl) { ev.preventDefault(); first.focus(); }
      }
    });
  }

  function open() {
    if (!dlg) build();
    last = document.activeElement;
    dlg.setAttribute('data-open', ''); document.documentElement.style.overflow = 'hidden';
    kind = 'All'; update(); input.focus(); input.select();
    document.querySelectorAll('.search-btn').forEach(function (b) { b.setAttribute('aria-expanded', 'true'); });
    load().then(update);
  }
  function close() {
    if (!dlg || !dlg.hasAttribute('data-open')) return;
    dlg.removeAttribute('data-open'); document.documentElement.style.overflow = '';
    document.querySelectorAll('.search-btn').forEach(function (b) { b.setAttribute('aria-expanded', 'false'); });
    if (last && last.focus) last.focus();
  }

  function wire() {
    document.querySelectorAll('.search-btn').forEach(function (b) {
      b.addEventListener('click', open);
      ['pointerenter', 'focus'].forEach(function (n) { b.addEventListener(n, load, { once: true }); });   // warm the index before the click
    });
  }
  document.addEventListener('keydown', function (ev) {
    var tag = (ev.target && ev.target.tagName) || '', typing = /^(INPUT|TEXTAREA|SELECT)$/.test(tag) || (ev.target && ev.target.isContentEditable);
    if ((ev.key === 'k' || ev.key === 'K') && (ev.metaKey || ev.ctrlKey)) { ev.preventDefault(); dlg && dlg.hasAttribute('data-open') ? close() : open(); }
    else if (ev.key === '/' && !typing && !ev.metaKey && !ev.ctrlKey) { ev.preventDefault(); open(); }
  });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', wire); else wire();
})();
