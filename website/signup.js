/* Email sign-up (worker/signup). Posts to the Worker and shows its answer in the status line; works without Turnstile. */
(function () {
  var form = document.querySelector('[data-signup]');
  if (!form) return;
  var note = form.querySelector('.signup-note'), btn = form.querySelector('button'), original = note.textContent;
  var siteKey = form.getAttribute('data-turnstile'), token = '', box;
  function say(t, bad) { note.textContent = t; note.classList.toggle('is-bad', !!bad); }
  if (siteKey) {
    var s = document.createElement('script');
    s.src = 'https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit'; s.async = true;
    s.onload = function () {
      box = document.createElement('div'); form.appendChild(box);
      window.turnstile.render(box, { sitekey: siteKey, size: 'flexible', callback: function (t) { token = t; } });
    };
    form.addEventListener('focusin', function once() { form.removeEventListener('focusin', once); document.head.appendChild(s); });
  }
  form.addEventListener('submit', function (e) {
    e.preventDefault();
    var email = form.elements.email.value.trim();
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(email)) { say('That does not look like an email address.', true); return; }
    if (siteKey && !token) { say('One moment, checking you are human, then press Sign up again.', true); return; }
    btn.disabled = true; say('Sending...');
    fetch(form.getAttribute('data-endpoint'), { method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ email: email, audience: form.getAttribute('data-audience'), turnstile: token, source: (location.pathname.split('/').pop() || 'index.html'), company: form.elements.company.value }) })
      .then(function (r) { return r.json().catch(function () { return {}; }).then(function (j) { return { ok: r.ok && j.ok, j: j }; }); })
      .then(function (r) {
        if (r.ok) { form.reset(); say(r.j.message || 'You are on the list.'); }
        else { say(r.j.error || 'Something went wrong. Please try again.', true); if (window.turnstile && box) { window.turnstile.reset(box); token = ''; } }
      })
      .catch(function () { say('Could not reach the server. Please try again.', true); })
      .then(function () { btn.disabled = false; });
  });
})();
