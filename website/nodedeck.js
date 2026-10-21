/* Nodes deck on the audience pages: zoomed node pictures in the fan deck (deck.js). Loops forever; swipe, drag, trackpad, arrows or tap. */
(function () {
  var root = document.querySelector('[data-nodedeck]');
  if (!root || !window.InfiniteDeck) return;
  var grid = root.querySelector('.nd-grid'), cards = Array.prototype.slice.call(grid.children);
  var t = root.querySelector('.nd-t'), d = root.querySelector('.nd-d');
  var deck = InfiniteDeck.create({
    stage: root, grid: grid, cardSel: '.nd-card', mode: 'fan', keys: false,
    getCards: function () { return cards; },
    onChange: function (i) { var c = cards[i]; if (!c) return; t.textContent = c.getAttribute('data-title'); d.textContent = c.getAttribute('data-desc'); var n = c.getAttribute('data-node'), h = n ? 'nodes/' + n + '.html' : 'nodes/'; t.href = h; }
  });
  deck.refresh(false);
  root.querySelectorAll('[data-nd]').forEach(function (b) { b.addEventListener('click', function () { deck.step(+b.getAttribute('data-nd')); }); });
  grid.addEventListener('keydown', function (e) {
    if (e.key === 'ArrowRight') { e.preventDefault(); deck.step(1); } else if (e.key === 'ArrowLeft') { e.preventDefault(); deck.step(-1); }
  });
  grid.addEventListener('click', function (e) {       // a tap on the front card opens its node page (a tap on another card just brings it forward)
    if (e.defaultPrevented) return;
    var c = e.target.closest('.nd-card');
    if (c && c === cards[deck.index]) { var n = c.getAttribute('data-node'); location.href = n ? 'nodes/' + n + '.html' : 'nodes/'; }
  });
})();
