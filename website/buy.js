// Buy page: opens Paddle checkout. Everything that differs between test and live lives in paddle.json (no code change at go-live).
(async () => {
  const $ = (id) => document.getElementById(id);
  let cfg;
  try { cfg = await (await fetch("paddle.json", { cache: "no-cache" })).json(); } catch { return; }
  if (cfg.environment === "sandbox") { $("buy-test").hidden = false; window.Paddle.Environment.set("sandbox"); }
  if (cfg.launchCode) { $("buy-now").textContent = "$" + cfg.launch; $("buy-was").textContent = "$" + cfg.regular; $("buy-was").hidden = false; $("buy-tag").hidden = false; }
  else $("buy-now").textContent = "$" + cfg.regular;
  const btn = $("buy-btn");
  // /buy.html?code=XXXX swaps the discount code (used for private test and partner codes).
  const q = (new URLSearchParams(location.search).get("code") || "").trim();
  const code = /^[A-Za-z0-9_-]{3,24}$/.test(q) ? q : cfg.launchCode;
  btn.textContent = cfg.released ? "Buy now" : "Pre-order";
  if (cfg.released) $("buy-ship").hidden = true;
  window.Paddle.Initialize({
    token: cfg.token,
    eventCallback: (e) => { if (e.name === "checkout.completed") { $("buy-done").hidden = false; $("buy-done").scrollIntoView({ behavior: "smooth", block: "center" }); } },
  });
  btn.disabled = false;
  btn.addEventListener("click", () => window.Paddle.Checkout.open({
    items: [{ priceId: cfg.priceId, quantity: 1 }],
    ...(code ? { discountCode: code } : {}),
    settings: { displayMode: "overlay", theme: document.documentElement.dataset.mode === "paper" ? "light" : "dark" },
  }));
  $("resend-form").addEventListener("submit", async (ev) => {
    ev.preventDefault();
    try { await fetch("/api/license/resend", { method: "POST", body: JSON.stringify({ email: ev.target.email.value }) }); } catch {}
    $("resend-msg").hidden = false;
  });
})();
