// Buy page: opens Paddle checkout. Everything that differs between test and live lives in paddle.json (no code change at go-live).
(async () => {
  const $ = (id) => document.getElementById(id);
  let cfg;
  try { cfg = await (await fetch("paddle.json", { cache: "no-cache" })).json(); } catch { return; }
  if (cfg.environment === "sandbox") { if ($("buy-test")) $("buy-test").hidden = false; window.Paddle.Environment.set("sandbox"); }
  if (cfg.launchCode) { $("buy-now").textContent = "$" + cfg.launch; $("buy-was").textContent = "$" + cfg.regular; $("buy-was").hidden = false; $("buy-tag").hidden = false; }
  else $("buy-now").textContent = "$" + cfg.regular;
  if (new URLSearchParams(location.search).get("done") && $("buy-done")) $("buy-done").hidden = false;
  const btn = $("buy-btn");
  // /buy.html?code=XXXX swaps the discount code (used for private test and partner codes).
  const q = (new URLSearchParams(location.search).get("code") || "").trim();
  const hasQueryCode = /^[A-Za-z0-9_-]{3,24}$/.test(q);
  const code = hasQueryCode ? q : cfg.launchCode;
  btn.textContent = cfg.released ? "Buy now" : "Pre-order";
  $("buy-ship").hidden = !!cfg.released;
  window.Paddle.Initialize({
    token: cfg.token,
    eventCallback: (e) => { if (e.name === "checkout.completed" && $("buy-done")) { $("buy-done").hidden = false; $("buy-done").scrollIntoView({ behavior: "smooth", block: "center" }); } else if (e.name === "checkout.completed") location.href = "buy.html?done=1"; },
  });
  if (hasQueryCode && window.Paddle.PricePreview) {
    window.Paddle.PricePreview({ items: [{ priceId: cfg.priceId, quantity: 1 }], discountCode: code })
      .then((res) => {
        const item = res?.data?.details?.lineItems?.[0];
        if (!item?.discounts?.length) return;
        const cents = Number(item.totals.subtotal) - Number(item.totals.discount);
        const cur = res.data.currencyCode || "USD";
        const fmt = (c) => new Intl.NumberFormat(undefined, { style: "currency", currency: cur, minimumFractionDigits: c % 100 === 0 ? 0 : 2 }).format(c / 100);
        $("buy-now").textContent = fmt(Math.max(0, cents));
        $("buy-was").textContent = fmt(Number(item.totals.subtotal));
        $("buy-was").hidden = false;
        $("buy-tag").textContent = "Code " + code;
        $("buy-tag").hidden = false;
      })
      .catch(() => {});
  }
  btn.disabled = false;
  btn.addEventListener("click", () => window.Paddle.Checkout.open({
    items: [{ priceId: cfg.priceId, quantity: 1 }],
    ...(code ? { discountCode: code } : {}),
    settings: { displayMode: "overlay", variant: "one-page", theme: document.documentElement.dataset.mode === "paper" ? "light" : "dark" },
  }));
  if ($("resend-form")) $("resend-form").addEventListener("submit", async (ev) => {
    ev.preventDefault();
    try { await fetch("/api/license/resend", { method: "POST", body: JSON.stringify({ email: ev.target.email.value }) }); } catch {}
    $("resend-msg").hidden = false;
  });
})();
