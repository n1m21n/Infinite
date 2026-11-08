// download.html: pick the visitor's platform tab; the tabs switch by hand too.
(() => {
  const tabs = [...document.querySelectorAll('.dl-tabs [role="tab"]')];
  const show = (id) => tabs.forEach((t) => {
    const on = t.id === id;
    t.setAttribute('aria-selected', String(on));
    document.getElementById(t.getAttribute('aria-controls')).hidden = !on;
  });
  tabs.forEach((t) => t.addEventListener('click', () => show(t.id)));
  const p = ((navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || navigator.userAgent).toLowerCase();
  const arm = /arm|aarch/.test(navigator.userAgent.toLowerCase());
  if (p.includes('win')) {
    show('tab-win');
    if (arm) { const b = document.querySelector('#os-win .btn-primary'); b.href = b.href.replace('x64', 'ARM64'); }
  } else if (p.includes('linux') && !/android/.test(navigator.userAgent.toLowerCase())) show('tab-linux');
})();
