/* Page audit: loads pages in same-origin iframes in each mode and width and reports horizontal overflow, text under 3:1 contrast
 * (on flat backgrounds) and text under 11.5px. Usage: serve website/, open a plain text page of it (e.g. /robots.txt), load this
 * file as a script, then: await window.__scan(['index.html'], ['midnight','paper'], [390, 1280]). */
window.__scan = async function (pages, modes, widths) {
  var res = [];
  function parse(c) { var m = c.match(/rgba?\(([^)]+)\)|color\(srgb ([^)]+)\)/); if (!m) return null; if (m[2]) { var a = m[2].replace('/', ' ').split(/\s+/).map(Number); return [a[0] * 255, a[1] * 255, a[2] * 255, a.length > 3 ? a[3] : 1]; } var b = m[1].split(/[ ,\/]+/).map(Number); return [b[0], b[1], b[2], b.length > 3 ? b[3] : 1]; }
  function lum(c) { var f = c.slice(0, 3).map(function (v) { v /= 255; return v <= .03928 ? v / 12.92 : Math.pow((v + .055) / 1.055, 2.4); }); return .2126 * f[0] + .7152 * f[1] + .0722 * f[2]; }
  function bgOf(el, w) { while (el && el.nodeType == 1) { var cs = w.getComputedStyle(el); var c = parse(cs.backgroundColor); if (c && c[3] > 0.6) return c; if (/gradient|url/.test(cs.backgroundImage)) return 'img'; el = el.parentElement; } return [255, 255, 255, 1]; }
  for (var m of modes) {
    localStorage.setItem('infinite-mode', m);
    for (var wd of widths) for (var p of pages) {
      var f = document.createElement('iframe'); f.style.cssText = 'position:fixed;left:-9999px;top:0;width:' + wd + 'px;height:900px;border:0'; f.src = '/' + p; document.body.appendChild(f);
      await new Promise(function (r) { f.onload = function () { setTimeout(r, 900); }; setTimeout(r, 6000); });
      try {
        var d = f.contentDocument, w = f.contentWindow, issues = [], seen = {};
        if (d.documentElement.scrollWidth > wd + 2) issues.push('H-OVERFLOW ' + d.documentElement.scrollWidth);
        var tw = d.createTreeWalker(d.body, NodeFilter.SHOW_TEXT), n;
        while (n = tw.nextNode()) {
          var t = n.textContent.trim(); if (t.length < 2) continue; var el = n.parentElement; if (!el || /SCRIPT|STYLE|NOSCRIPT/.test(el.tagName)) continue;
          var r = el.getBoundingClientRect(); if (!r.width || !r.height) continue; var cs = w.getComputedStyle(el); if (cs.visibility == 'hidden' || cs.display == 'none' || +cs.opacity == 0) continue;
          var fg = parse(cs.color), bg = bgOf(el, w); if (bg == 'img' || !fg) continue; var a = fg[3];
          var eff = [0, 1, 2].map(function (i) { return fg[i] * a + bg[i] * (1 - a); }); var L1 = lum(eff), L2 = lum(bg), cr = (Math.max(L1, L2) + .05) / (Math.min(L1, L2) + .05), fs = parseFloat(cs.fontSize);
          var s = el.tagName.toLowerCase() + '.' + String(el.className).split(' ')[0];
          if (cr < 3.0 && !seen['c' + s]) { seen['c' + s] = 1; issues.push('LOWCONTRAST ' + cr.toFixed(1) + ' ' + s + ' "' + t.slice(0, 20) + '"'); }
          if (fs < 11.5 && !seen['f' + s]) { seen['f' + s] = 1; issues.push('SMALL ' + fs + ' ' + s + ' "' + t.slice(0, 16) + '"'); }
        }
        if (issues.length) res.push(p + ' ' + m + ' ' + wd + '\n  ' + issues.join('\n  '));
      } catch (e) { res.push(p + ' ERR ' + e); }
      f.remove();
    }
  }
  localStorage.setItem('infinite-mode', 'midnight'); return res.join('\n') || 'clean';
};
