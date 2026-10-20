/* Button audit: lists every button-like element on the site and whether it has the soft 3D (neumorphic) look.
 *
 * What: loads each page in a same-origin iframe, collects <button>, a.btn*, [role=button|tab], input[type=submit|button]
 *       and any link styled as a pill/tile, and groups them by "tag.class". A button is "soft" when neumo.css marked it
 *       (it carries the --nm-raise shadow, found as an inset highlight plus a drop shadow and a gradient background).
 * Why:  buttons live in a dozen stylesheets and drifted; this finds the ones that were missed.
 * Usage: serve website/ (python3 -m http.server 8791 --directory website), open any page of it in a browser, paste this file
 *        into the console (or javascript_tool), then read window.__buttonAudit.report. Set window.__auditPages to a list of
 *        paths to override the default list.
 */
(async function () {
  var PAGES = window.__auditPages || ['index.html', 'artists.html', 'musicians.html', 'vjs.html', 'creative-coders.html', 'developers.html', 'library.html',
    'artworks.html', 'benchmarks.html', 'extensions.html', 'philosophy.html', 'system.html', 'blog/index.html', 'library/first-sound.html', 'embeds/index.html'];
  var SEL = 'button, a.btn, a[class*=btn], [role=button], [role=tab], input[type=submit], input[type=button], summary, a.pill, a.pw, a.ax-key, a.ax-ghost, a.reel-btn, .copy, .run';
  var sig = function (el) { return el.tagName.toLowerCase() + (el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).filter(function (c) { return !/^(on|live|is-|active|open)/.test(c); }).join('.') : ''); };
  var out = {};
  for (var p of PAGES) {
    var f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:-9999px;top:0;width:1280px;height:900px;border:0';
    f.src = '/' + p; document.body.appendChild(f);
    await new Promise(function (r) { f.onload = function () { setTimeout(r, 900); }; setTimeout(r, 6000); });
    try {
      var d = f.contentDocument, w = f.contentWindow;
      d.querySelectorAll(SEL).forEach(function (el) {
        var r = el.getBoundingClientRect(); if (!r.width || !r.height) return;
        var cs = w.getComputedStyle(el), s = sig(el);
        var soft = /inset/.test(cs.boxShadow) && /gradient/.test(cs.backgroundImage);
        var o = out[s] = out[s] || { sig: s, soft: 0, flat: 0, pages: {}, sample: (el.textContent || el.getAttribute('aria-label') || '').trim().slice(0, 24) };
        soft ? o.soft++ : o.flat++; o.pages[p] = 1;
      });
    } catch (e) { out['ERR ' + p] = { sig: 'ERR ' + p, soft: 0, flat: 0, pages: {}, sample: String(e) }; }
    f.remove();
  }
  var rows = Object.values(out).sort(function (a, b) { return b.flat - a.flat; });
  window.__buttonAudit = { rows: rows, report: rows.filter(function (r) { return r.flat; }).map(function (r) { return r.flat + ' flat / ' + r.soft + ' soft  ' + r.sig + '  [' + r.sample + ']  ' + Object.keys(r.pages).length + ' pages'; }) };
  return window.__buttonAudit.report.length + ' flat groups';
})();
