// One share behaviour for the whole site: phones get the native share sheet, everything else copies the link.
// Any button with class share-btn shares its page (data-share-url overrides); .smp-shb on /samples/ has its own handler.
(function () {
  function pageUrl(b) {
    var u = b.getAttribute('data-share-url');
    if (u) return new URL(u, location.href).href;
    var c = document.querySelector('link[rel="canonical"]');
    return c && c.href ? c.href : location.origin + location.pathname;
  }
  function flash(b) {
    b.classList.add('copied'); var t = b.getAttribute('title'); b.setAttribute('title', 'Link copied');
    setTimeout(function () { b.classList.remove('copied'); b.setAttribute('title', t || 'Share'); }, 1600);
  }
  document.addEventListener('click', function (e) {
    var b = e.target.closest('.share-btn');
    if (!b) return;
    e.preventDefault();
    var url = pageUrl(b), title = (document.title || 'Infinite').replace(/\s*\|\s*Infinite$/, '');
    function copy() {
      if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(url).then(function () { flash(b); }, function () { window.prompt('Copy this link', url); });
      else window.prompt('Copy this link', url);
    }
    if (navigator.share) navigator.share({ title: title, url: url }).catch(function (err) { if (err && err.name !== 'AbortError') copy(); });
    else copy();
  });
})();
