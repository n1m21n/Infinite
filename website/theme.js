/* Theme: Midnight (dark) is the default; the header button switches to Paper (light) and remembers it. */
(function () {
  var KEY = 'infinite-mode', root = document.documentElement;
  function saved() { try { return localStorage.getItem(KEY); } catch (e) { return null; } }
  function apply(mode) {
    root.setAttribute('data-mode', mode);
    var m = document.querySelector('meta[name="theme-color"]');
    if (m) m.setAttribute('content', mode === 'paper' ? '#FBFAF6' : '#151930');
    var b = document.querySelectorAll('[data-mode-toggle]');
    for (var i = 0; i < b.length; i++) {
      b[i].setAttribute('aria-pressed', mode === 'paper' ? 'true' : 'false');
      b[i].setAttribute('aria-label', mode === 'paper' ? 'Switch to dark mode' : 'Switch to light mode');
      b[i].setAttribute('title', mode === 'paper' ? 'Dark mode' : 'Light mode');
    }
  }
  var dflt = window.INFINITE_DEFAULT_MODE === 'paper' ? 'paper' : 'midnight';
  apply(saved() === 'paper' || saved() === 'midnight' ? saved() : dflt);
  document.addEventListener('DOMContentLoaded', function () {
    apply(root.getAttribute('data-mode'));
    document.addEventListener('click', function (e) {
      var t = e.target.closest && e.target.closest('[data-mode-toggle]');
      if (!t) return;
      var next = root.getAttribute('data-mode') === 'paper' ? 'midnight' : 'paper';
      apply(next);
      try { localStorage.setItem(KEY, next); } catch (err) {}
    });
  });
})();
