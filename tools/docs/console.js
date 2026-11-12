/* Valhalla test console: runs every endpoint with dummy data, in dependency order, straight from the docs page. */
(function () {
  'use strict';
  const STEPS = window.__VC_STEPS__ || [];
  const byId = Object.fromEntries(STEPS.map((s) => [s.id, s]));
  const producer = {};
  STEPS.forEach((s) => Object.keys(s.saves || {}).forEach((v) => { if (!producer[v]) producer[v] = s.id; }));
  const CONST = new Set(['RUN', 'PROVIDER_USERNAME', 'PROVIDER_PASSWORD', 'USER_USERNAME', 'USER_PASSWORD', 'DISP_PROVIDER', 'DISP_USER']);

  let state = {};
  let results = {};
  let busy = false;
  let stopRequested = false;
  const rows = {};

  const el = (tag, attrs, ...kids) => {
    const e = document.createElement(tag);
    Object.entries(attrs || {}).forEach(([k, v]) => {
      if (k === 'class') e.className = v;
      else if (k === 'text') e.textContent = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else e.setAttribute(k, v);
    });
    kids.flat().forEach((c) => { if (c != null) e.append(c); });
    return e;
  };

  /* ---------- session state ---------- */
  function newSession() {
    const run = String(Date.now()).slice(-6);
    state = {
      RUN: run, PROVIDER_USERNAME: 'provider' + run, PROVIDER_PASSWORD: '1234@Abc!', USER_USERNAME: 'user' + run,
      USER_PASSWORD: '123Mm@p0', DISP_PROVIDER: 'providerx' + run, DISP_USER: 'userx' + run,
    };
    results = {};
    STEPS.forEach((s) => paintRow(s, null));
    paintState();
    paintSummary();
  }

  class Missing extends Error { constructor(v) { super('missing ' + v); this.v = v; } }
  const vars = (text) => Array.from((text || '').matchAll(/\{\{([A-Z_]+)\}\}/g)).map((m) => m[1]);
  function needsOf(s) {
    const own = Object.keys(s.set || {});
    const names = new Set();
    [s.path, s.query, s.body === '@empty' ? '' : s.body].concat(Object.values(s.set || {})).forEach((t) => vars(t).forEach((v) => names.add(v)));
    if (s.auth) names.add(s.auth);
    (s.needs || []).forEach((v) => names.add(v));
    return Array.from(names).filter((v) => !CONST.has(v) && !own.includes(v));
  }
  function sub(text, local) {
    return (text || '').replace(/\{\{([A-Z_]+)\}\}/g, (m, v) => {
      const val = local && v in local ? local[v] : state[v];
      if (val === undefined) throw new Missing(v);
      return val;
    });
  }

  /* ---------- running ---------- */
  const baseUrl = () => baseInput.value.trim().replace(/\/+$/, '');

  async function runStep(id, opts) {
    const s = byId[id];
    opts = opts || { auto: true };
    const auto = [];
    for (const v of needsOf(s)) {
      if (state[v] !== undefined) continue;
      const p = producer[v];
      if (!opts.auto || !p || p === id) return finish(s, { status: 'skip', note: 'needs ' + v });
      auto.push(byId[p].name);
      await runStep(p, { auto: true });
      if (state[v] === undefined) return finish(s, { status: 'skip', note: 'could not get ' + v + ' (see ' + byId[p].name + ')' });
    }
    paintRow(s, { status: 'run' });
    const out = { status: 'fail', auto };
    try {
      const local = {};
      Object.entries(s.set || {}).forEach(([k, t]) => { local[k] = sub(t, local); });
      const url = baseUrl() + sub(s.path, local) + (s.query ? '?' + sub(s.query, local) : '');
      const headers = {};
      let body;
      if (s.auth) headers.Authorization = 'Bearer ' + state[s.auth];
      if (s.body === '@empty') headers['Content-Type'] = 'application/json';
      else if (s.body) {
        headers['Content-Type'] = 'application/json';
        body = JSON.stringify(JSON.parse(sub(s.body, local)));
      }
      out.req = { method: s.method, url, headers, body };
      const t0 = performance.now();
      let resp, text;
      try {
        resp = await fetch(url, { method: s.method, headers, body });
        text = await resp.text();
      } catch (e) {
        out.ms = Math.round(performance.now() - t0);
        out.note = 'Could not reach the server (network error or CORS). Open this page from the server itself, e.g. ' +
          'http://127.0.0.1:8080/api/v2/doc, or fix the Base URL above.';
        out.res = { status: 0, text: String(e) };
        return finish(s, out);
      }
      out.ms = Math.round(performance.now() - t0);
      let json;
      try { json = JSON.parse(text); } catch (e) { json = undefined; }
      out.res = { status: resp.status, text, json };
      const expect = s.expect || 200;
      if (resp.status === expect) {
        out.status = 'pass';
        for (const [v, key] of Object.entries(s.saves || {})) {
          const val = json && json[key];
          if (val === undefined || val === null || val === '') {
            out.status = 'fail';
            out.note = 'response has no "' + key + '" field, so later steps cannot continue';
            break;
          }
          state[v] = String(val);
        }
        if (out.status === 'pass') (s.clears || []).forEach((v) => { delete state[v]; });
      } else {
        out.note = 'expected HTTP ' + expect + ', got ' + resp.status;
        if (resp.status === 401 && s.auth) out.note += ' - the token may have expired or the account is suspended; press Reset and run again';
      }
    } catch (e) {
      out.note = e instanceof Missing ? 'missing value ' + e.v : 'invalid payload template: ' + e.message;
    }
    return finish(s, out);
  }

  function finish(s, r) {
    results[s.id] = r;
    paintRow(s, r);
    paintState();
    paintSummary();
    if (state.PROVIDER_TOKEN) syncSwaggerAuth();
    return r;
  }

  async function runAll() {
    if (busy) return;
    busy = true; stopRequested = false;
    setBusy(true);
    newSession();
    const withSuspend = optSuspend.checked;
    const withCleanup = optCleanup.checked;
    for (const s of STEPS) {
      if (stopRequested) { finish(s, { status: 'skip', note: 'stopped' }); continue; }
      if (s.optional && !withSuspend) { finish(s, { status: 'skip', note: 'suspend check not selected' }); continue; }
      if (s.cleanup && !withCleanup) { finish(s, { status: 'skip', note: 'clean-up not selected' }); continue; }
      await runStep(s.id, { auto: false });
    }
    busy = false;
    setBusy(false);
    paintSummary(true);
  }

  async function runOne(id) {
    if (busy) return;
    busy = true; setBusy(true);
    try { await runStep(id, { auto: true }); } finally { busy = false; setBusy(false); }
  }

  /* ---------- painting ---------- */
  const ICON = { idle: '○', run: '◔', pass: '✓', fail: '✗', skip: '–' };
  const short = (t) => (t.length > 14 ? t.slice(0, 14) + '…' : t);
  const pretty = (text) => { try { return JSON.stringify(JSON.parse(text), null, 2); } catch (e) { return text; } };

  function curlOf(r) {
    if (!r || !r.req) return '';
    const parts = ["curl -sS -X " + r.req.method + " '" + r.req.url + "'"];
    Object.entries(r.req.headers).forEach(([k, v]) => parts.push("-H '" + k + ': ' + v + "'"));
    if (r.req.body) parts.push("-d '" + r.req.body.replace(/'/g, "'\\''") + "'");
    return parts.join(' \\\n  ');
  }
  function copy(text, btn) {
    const done = () => { const old = btn.textContent; btn.textContent = 'Copied'; setTimeout(() => { btn.textContent = old; }, 1200); };
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(text).then(done, done);
    else { const t = el('textarea'); t.value = text; document.body.append(t); t.select(); try { document.execCommand('copy'); } catch (e) { /* ignore */ } t.remove(); done(); }
  }

  function paintRow(s, r) {
    const row = rows[s.id];
    if (!row) return;
    const st = r ? r.status : 'idle';
    row.el.className = 'vc-row vc-' + st;
    if (st === 'fail') row.section.open = true; // collapsed sections open themselves when something fails
    row.icon.textContent = ICON[st];
    row.http.textContent = r && r.res ? String(r.res.status || 'ERR') : '';
    row.ms.textContent = r && r.ms !== undefined ? r.ms + ' ms' : '';
    row.note.textContent = r && r.note ? r.note : (r && r.auto && r.auto.length ? 'ran first: ' + r.auto.join(', ') : '');
    row.detail.textContent = '';
    if (r && (r.req || r.res)) {
      const reqTxt = r.req ? r.req.method + ' ' + r.req.url + '\n' + Object.entries(r.req.headers).map(([k, v]) => k + ': ' + (k === 'Authorization' ? short(v) : v)).join('\n') +
        (r.req.body ? '\n\n' + pretty(r.req.body) : '') : '';
      const resTxt = r.res ? 'HTTP ' + r.res.status + '\n\n' + pretty(r.res.text || '') : '';
      const copyCurl = el('button', { class: 'vc-mini', type: 'button', text: 'Copy as curl', onclick: (e) => copy(curlOf(r), e.target) });
      const copyRes = el('button', { class: 'vc-mini', type: 'button', text: 'Copy response', onclick: (e) => copy(r.res ? pretty(r.res.text || '') : '', e.target) });
      row.detail.append(
        el('div', { class: 'vc-cols' },
          el('div', {}, el('h4', { text: 'Request' }), el('pre', { text: reqTxt })),
          el('div', {}, el('h4', { text: 'Response' }), el('pre', { text: resTxt }))),
        el('div', { class: 'vc-actions' }, copyCurl, copyRes));
    }
  }

  const CHIPS = [['provider', 'PROVIDER_ID', 'PROVIDER_TOKEN'], ['user', 'USER_ID', 'USER_TOKEN'], ['clinic', 'CLINIC_ID'], ['pharmacy', 'PHARMACY_ID'],
    ['laboratory', 'LAB_ID'], ['radiology', 'RAD_ID'], ['patient', 'PATIENT_ID'], ['visit', 'VISIT_ID'], ['appointment', 'APPOINTMENT_ID']];
  function paintState() {
    chipsBox.textContent = '';
    CHIPS.forEach(([label, idVar, tokVar]) => {
      const has = state[idVar] !== undefined;
      chipsBox.append(el('span', { class: 'vc-chip' + (has ? ' on' : '') }, label + (has ? ' #' + state[idVar] : ' –') + (tokVar && state[tokVar] ? ' 🔑' : '')));
    });
  }

  function paintSummary(final) {
    const all = Object.values(results);
    const c = { pass: 0, fail: 0, skip: 0 };
    all.forEach((r) => { if (c[r.status] !== undefined) c[r.status]++; });
    const total = STEPS.length;
    const doneN = all.filter((r) => r.status !== 'run').length;
    bar.style.width = (total ? (doneN / total) * 100 : 0) + '%';
    bar.className = 'vc-bar' + (c.fail ? ' bad' : '');
    summary.textContent = doneN
      ? c.pass + ' passed · ' + c.fail + ' failed · ' + c.skip + ' skipped' + (final ? (c.fail ? '  — something needs attention (expand red rows)' : '  — all good') : '')
      : 'Nothing run yet';
    const dot = document.querySelector('#vc-launch .vc-dot');
    if (dot) { dot.textContent = doneN ? (c.fail ? c.fail + ' failed' : c.pass + ' ✓') : ''; dot.className = 'vc-dot' + (c.fail ? ' bad' : ''); dot.style.display = doneN ? '' : 'none'; }
    summary.className = 'vc-summary' + (final ? (c.fail ? ' bad' : ' good') : '');
  }

  function setBusy(b) {
    document.querySelectorAll('.vc-run, #vc-runall, #vc-reset').forEach((x) => { x.disabled = b; });
    stopBtn.disabled = !b;
    runAllBtn.textContent = b ? 'Running…' : '▶ Run all tests';
  }

  function syncSwaggerAuth() {
    try {
      if (window.ui && window.ui.authActions && state.PROVIDER_TOKEN) {
        window.ui.authActions.authorize({ bearerAuth: { name: 'bearerAuth', schema: { type: 'http', scheme: 'bearer' }, value: state.PROVIDER_TOKEN } });
      }
    } catch (e) { /* swagger not ready */ }
  }

  function openRow(id) {
    const row = rows[id];
    if (!row) return;
    row.section.open = true;
    row.el.classList.add('open');
    const box = root, r = row.el.getBoundingClientRect(), b = box.getBoundingClientRect();
    if (r.top < b.top || r.bottom > b.bottom) box.scrollTo({ top: box.scrollTop + r.top - b.top - 80 });
  }

  /* ---------- build the UI ---------- */
  const qs = new URLSearchParams(location.search);
  let defaultBase = qs.get('base') || (location.protocol.indexOf('http') === 0 ? location.origin + '/api/v2' : 'http://127.0.0.1:8080/api/v2');
  try { const saved = localStorage.getItem('vc.base'); if (saved && !qs.get('base') && location.protocol.indexOf('http') !== 0) defaultBase = saved; } catch (e) { /* storage blocked */ }

  const baseInput = el('input', { id: 'vc-base', type: 'text', value: defaultBase, spellcheck: 'false' });
  baseInput.addEventListener('change', () => { try { localStorage.setItem('vc.base', baseInput.value); } catch (e) { /* ignore */ } });
  const pingOut = el('span', { class: 'vc-ping' });
  const pingBtn = el('button', { class: 'vc-btn', type: 'button', text: 'Check connection', onclick: async () => {
    pingOut.className = 'vc-ping'; pingOut.textContent = 'checking…';
    const t0 = performance.now();
    try {
      const r = await fetch(baseUrl() + '/hello');
      pingOut.className = 'vc-ping ' + (r.ok ? 'good' : 'bad');
      pingOut.textContent = r.ok ? 'Server reachable (' + Math.round(performance.now() - t0) + ' ms)' : 'Server answered HTTP ' + r.status;
    } catch (e) {
      pingOut.className = 'vc-ping bad';
      pingOut.textContent = 'Cannot reach it. Is the server running? If you opened this file directly, open http://127.0.0.1:8080/api/v2/doc instead.';
    }
  } });
  const runAllBtn = el('button', { id: 'vc-runall', class: 'vc-btn primary', type: 'button', text: '▶ Run all tests', onclick: runAll });
  const stopBtn = el('button', { class: 'vc-btn', type: 'button', text: '■ Stop', disabled: 'disabled', onclick: () => { stopRequested = true; } });
  const resetBtn = el('button', { id: 'vc-reset', class: 'vc-btn', type: 'button', text: '↺ Reset', onclick: () => { if (!busy) newSession(); } });
  const optCleanup = el('input', { type: 'checkbox', checked: 'checked' });
  const optSuspend = el('input', { type: 'checkbox' });
  const bar = el('div', { class: 'vc-bar' });
  const summary = el('span', { class: 'vc-summary', text: 'Nothing run yet' });
  const chipsBox = el('div', { class: 'vc-chips' });
  const list = el('div', { class: 'vc-list' });

  const sections = [];
  STEPS.forEach((s) => { if (!sections.includes(s.section)) sections.push(s.section); });
  sections.forEach((name) => {
    const group = STEPS.filter((s) => s.section === name);
    const det = el('details', { class: 'vc-section' });
    det.append(el('summary', {}, name, el('span', { class: 'vc-count', text: ' · ' + group.length + (group[0].optional ? ' (optional)' : '') })));
    group.forEach((s) => {
      const icon = el('span', { class: 'vc-ic', text: ICON.idle });
      const http = el('span', { class: 'vc-http' });
      const ms = el('span', { class: 'vc-ms' });
      const note = el('span', { class: 'vc-note' });
      const detail = el('div', { class: 'vc-detail' });
      const run = el('button', { class: 'vc-run', type: 'button', title: 'Run this endpoint (prerequisites run first)', text: '▶', onclick: (e) => { e.stopPropagation(); rows[s.id].el.classList.add('open'); runOne(s.id); } });
      const line = el('div', { class: 'vc-line', onclick: () => rows[s.id].el.classList.toggle('open') },
        run, icon, el('span', { class: 'vc-m ' + s.method, text: s.method }), el('code', { class: 'vc-path', text: s.path + (s.query ? '?' + s.query.replace(/\{\{[A-Z_]+\}\}/g, '…') : '') }),
        el('span', { class: 'vc-name', text: s.name }), http, ms, note);
      const row = el('div', { class: 'vc-row', id: 'vc-' + s.id }, line, detail);
      rows[s.id] = { el: row, icon, http, ms, note, detail, section: det };
      det.append(row);
    });
    list.append(det);
  });

  const root = el('section', { id: 'console', class: 'vc-root' },
    el('div', { class: 'vc-top' },
      el('div', {},
        el('h2', { text: 'Test console' }),
        el('p', { text: 'Run every endpoint against your running server with ready-made dummy data. Nothing to fill in: ids and tokens from earlier calls are reused automatically.' })),
      el('div', { class: 'vc-tools' },
        el('button', { class: 'vc-icon', type: 'button', title: 'Switch dark / light', text: '◐', onclick: () => setTheme(host.classList.contains('light') ? 'dark' : 'light') }),
        el('button', { class: 'vc-icon', type: 'button', title: 'Close (Esc)', text: '✕', onclick: () => closePanel() }))),
    el('ol', { class: 'vc-how' },
      el('li', {}, 'Check the ', el('b', { text: 'Base URL' }), ' (already set for the server that served this page).'),
      el('li', {}, 'Press ', el('b', { text: '▶ Run all tests' }), ', or press ', el('b', { text: '▶' }), ' beside any endpoint. Missing prerequisites (e.g. a clinic before a patient) are created for you.'),
      el('li', {}, 'Green ✓ passed, red ✗ failed. Click a row to see the exact request and response, or copy it as a curl command.')),
    el('div', { class: 'vc-toolbar' },
      el('label', { class: 'vc-field' }, 'Base URL', baseInput), pingBtn, pingOut),
    el('div', { class: 'vc-toolbar' },
      runAllBtn, stopBtn, resetBtn,
      el('label', { class: 'vc-opt' }, optCleanup, ' Delete what the test created (clean-up)'),
      el('label', { class: 'vc-opt', title: 'Suspended accounts cannot be re-activated or deleted through the API' }, optSuspend, ' Also test suspend (leaves 2 suspended accounts)')),
    el('div', { class: 'vc-progress' }, el('div', { class: 'vc-track' }, bar), summary),
    chipsBox,
    el('p', { class: 'vc-hint', text: 'Tip: after login the provider token is also filled into the Authorize button below, so "Try it out" in the reference works right away.' }),
    list);

  const host = document.getElementById('vc-root');
  const backdrop = el('div', { class: 'vc-backdrop', onclick: () => closePanel() });
  host.append(backdrop, root);
  function setTheme(t) {
    document.querySelectorAll('#vc-root,#vg-root').forEach((r) => r.classList.toggle('light', t === 'light'));
    document.documentElement.classList.toggle('vc-light', t === 'light');
    try { localStorage.setItem('vc.theme', t); } catch (e) { /* ignore */ }
  }
  let dark = false; try { dark = localStorage.getItem('vc.theme') === 'dark'; } catch (e) { /* ignore */ }
  if (!dark) { host.classList.add('light'); document.documentElement.classList.add('vc-light'); } // light is the default
  const launch = el('button', { id: 'vc-launch', type: 'button', onclick: () => openPanel() }, '▶ Test console', el('span', { class: 'vc-dot', text: '' }));
  document.body.append(launch);
  function openPanel() { const g = document.getElementById('vg-root'); if (g) g.classList.remove('open'); host.classList.add('open'); document.body.style.overflow = 'hidden'; }
  function closePanel() { host.classList.remove('open'); document.body.style.overflow = ''; }
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closePanel(); });

  document.addEventListener('click', (e) => {
    const a = e.target && e.target.closest ? e.target.closest('a[href^="#vc:"]') : null;
    if (!a) return;
    e.preventDefault();
    e.stopPropagation();
    const id = a.getAttribute('href').slice(4);
    if (!byId[id]) return;
    openPanel();
    openRow(id);
    runOne(id);
  }, true);

  window.valhallaConsole = { open: openPanel, close: closePanel, runAll, runStep: runOne, reset: newSession, state: () => state, results: () => results, steps: STEPS };
  newSession();
})();
