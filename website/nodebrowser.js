/* The node browser on nodes/: the same fan deck and HUD as the audience pages (deck.js), over all 300 nodes. It opens on a random node,
   the family menu narrows it, and only the cards near the front keep their picture so a phone never runs out of memory. */
(function () {
  var root = document.getElementById('ns'), nd = root && root.querySelector('[data-nodedeck]');
  if (!nd || !window.InfiniteDeck) return;
  document.documentElement.classList.add('js');
  var grid = nd.querySelector('.nd-grid'), all = Array.prototype.slice.call(grid.children), cards = all;
  var t = nd.querySelector('.nd-t'), d = nd.querySelector('.nd-d');
  var KEEP = 7;                                       // cards either side of the front that may hold a picture

  function spin(list) {                               // a random card in front: rotate the list so it comes first
    var k = Math.floor(Math.random() * list.length);
    return list.slice(k).concat(list.slice(0, k));
  }
  function trim(front) {
    var n = cards.length;
    cards.forEach(function (c, i) {
      var dist = Math.abs(i - front); dist = Math.min(dist, n - dist);
      var im = c.querySelector('img');
      if (dist > KEEP && im && im.getAttribute('src')) { im.setAttribute('data-src', im.getAttribute('src')); im.removeAttribute('src'); }
    });
  }
  var deck = InfiniteDeck.create({
    stage: nd, grid: grid, cardSel: '.nd-card', mode: 'fan', keys: false,
    getCards: function () { return cards; },
    onChange: function (i) {
      var c = cards[i]; if (!c) return;
      t.textContent = c.getAttribute('data-title'); d.textContent = c.getAttribute('data-desc');
      var h = c.getAttribute('data-href'); t.href = h;
      trim(i);
    }
  });
  cards = spin(all); deck.refresh(false);
  nd.querySelectorAll('[data-nd]').forEach(function (b) { b.addEventListener('click', function () { deck.step(+b.getAttribute('data-nd')); }); });
  grid.addEventListener('keydown', function (e) {
    if (e.key === 'ArrowRight') { e.preventDefault(); deck.step(1); } else if (e.key === 'ArrowLeft') { e.preventDefault(); deck.step(-1); }
  });
  grid.addEventListener('click', function (e) {       // a tap on the front card opens its page (the deck brings other cards forward itself)
    if (e.defaultPrevented) return;
    var c = e.target.closest('.nd-card');
    if (c && c === cards[deck.index]) location.href = c.getAttribute('data-href');
  });

  var sel = root.querySelector('.nat-pick');
  sel.addEventListener('change', function () {
    var f = sel.value || '';
    var keep = all.filter(function (a) { return !f || a.getAttribute('data-fam') === f; });
    all.forEach(function (a) { if (keep.indexOf(a) < 0) { a.style.display = 'none'; var im = a.querySelector('img'); if (im && im.getAttribute('src')) { im.setAttribute('data-src', im.getAttribute('src')); im.removeAttribute('src'); } } });
    cards = spin(keep); deck.refresh(false);
  });
})();
