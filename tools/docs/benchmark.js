/* Valhalla benchmark panel: fires many requests at one endpoint, or at every endpoint, and reports latency and throughput.
   Write endpoints are benchmarked too: records are created with unique data, then deleted again (and the delete is timed). */
(function () {
  'use strict';
  const C = window.valhallaConsole;
  const host = document.getElementById('vb-root');
  if (!C || !host) return;
  const STEPS = C.steps;

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
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  /* ---------- which endpoints are what ---------- */
  const noKeys = (o) => !Object.keys(o || {}).length;
  const plain = (s) => !s.cleanup && !s.optional;
  /* reads: GET, or POST /read and /search that save and clear nothing */
  const isRead = (s) => plain(s) && noKeys(s.saves) && !(s.clears || []).length && (s.method === 'GET' || /\/(read|search)$/.test(s.path));
  /* updates and activate can be repeated safely: they change an existing record to the same values */
  const isRepeatable = (s) => plain(s) && noKeys(s.saves) && ((s.method === 'PUT' && /\/update$/.test(s.path)) || /\/activate$/.test(s.path));

  /* login can be repeated (each call just issues a token); the delete endpoints the API does not implement answer 400 by design */
  const isLogin = (s) => !s.optional && s.method === 'POST' && /\/login$/.test(s.path);
  const isUnsupportedDelete = (s) => s.cleanup && s.method === 'DELETE' && s.expect === 400;
  const GROUPS = ['users', 'providers'];

  /* create + delete pairs: N records are created with unique data (timed), then deleted again (timed) */
  const WRITES = [
    { key: 'appointments', create: '/appointments/clinics/create', del: '/appointments/clinics/delete' },
    { key: 'visits', create: '/clinic/visits/create', del: '/clinic/visits/delete' },
    { key: 'patients', create: '/clinic/patients/create', del: '/clinic/patients/delete', vary: 'email' },
    { key: 'clinics', create: '/services/clinics/create', del: '/services/clinics/delete' },
    { key: 'pharmacies', create: '/services/pharmacies/create', del: '/services/pharmacies/delete' },
    { key: 'laboratories', create: '/services/laboratories/create', del: '/services/laboratories/delete' },
    { key: 'radiologycenters', create: '/services/radiologycenters/create', del: '/services/radiologycenters/delete' },
    { key: 'users', create: '/clients/users/create', del: '/clients/users/delete', vary: 'account', login: '/clients/users/login' },
    { key: 'providers', create: '/clients/providers/create', del: '/clients/providers/delete', vary: 'account', login: '/clients/providers/login' },
  ];
  const stepAt = (path, method) => STEPS.find((s) => s.path === path && s.method === method && plain(s));
  const writeOf = (s) => WRITES.find((w) => s.method === 'POST' && s.path === w.create && plain(s));

  let running = false;
  let stop = false;

  /* ---------- rate-limit budget: the server's DoS filter allows 10 000 requests / 30 s by default ---------- */
  const sentLog = [];
  const BUDGET = 8000, WINDOW = 30000;
  async function reserve(n) {
    if (n > BUDGET) return;
    for (;;) {
      const now = performance.now();
      while (sentLog.length && now - sentLog[0][0] > WINDOW) sentLog.shift();
      const used = sentLog.reduce((a, x) => a + x[1], 0);
      if (used + n <= BUDGET || stop) break;
      setStatus('Pausing so the server\'s rate limit (10 000 requests / 30 s) is not hit: ' + used + ' requests sent in the last 30 s…');
      await sleep(1000);
    }
    sentLog.push([performance.now(), n]);
  }

  /* ---------- engine ---------- */
  const pct = (sorted, p) => sorted[Math.min(sorted.length - 1, Math.max(0, Math.ceil((p / 100) * sorted.length) - 1))];

  /* make(i) returns request i: {method,url,headers,body,expect}. collect=true keeps {status,json} per request. */
  async function hammer(make, total, conc, onTick, collect) {
    const lat = [], codes = {}, items = [];
    let sent = 0, done = 0, ok = 0, bytes = 0;
    const t0 = performance.now();
    const worker = async () => {
      while (!stop && sent < total) {
        const i = sent++;
        const req = make(i);
        const a = performance.now();
        let status = 0, json;
        try {
          const r = await fetch(req.url, { method: req.method, headers: req.headers, body: req.body, cache: 'no-store' });
          status = r.status;
          const text = await r.text();
          bytes += text.length;
          if (collect) { try { json = JSON.parse(text); } catch (e) { json = undefined; } }
        } catch (e) { status = 0; }
        lat.push(performance.now() - a);
        codes[status || 'ERR'] = (codes[status || 'ERR'] || 0) + 1;
        if (status === req.expect) ok++;
        if (collect) items[i] = { status, json, ok: status === req.expect };
        done++;
        if (onTick) onTick(done, total);
      }
    };
    await Promise.all(Array.from({ length: Math.min(conc, total) }, worker));
    const secs = (performance.now() - t0) / 1000;
    const sorted = lat.slice().sort((x, y) => x - y);
    const n = sorted.length;
    return {
      n, ok, errors: n - ok, codes, secs, bytes, lat: sorted, items,
      rps: n / (secs || 1),
      min: n ? sorted[0] : 0, max: n ? sorted[n - 1] : 0,
      avg: n ? sorted.reduce((a, b) => a + b, 0) / n : 0,
      p50: n ? pct(sorted, 50) : 0, p90: n ? pct(sorted, 90) : 0, p95: n ? pct(sorted, 95) : 0, p99: n ? pct(sorted, 99) : 0,
    };
  }

  /* unique data for request i so that creates do not collide on username / email */
  function vary(req, i, kind) {
    if (!kind) return req;
    const b = JSON.parse(req.body);
    const run = String(C.state().RUN);
    if (kind === 'account') { b.username = 'bm' + run + 'u' + i; b.email = b.username + '@mail.com'; }
    else if (kind === 'suspend') { b.username = 'bs' + run + 's' + i; b.email = b.username + '@mail.com'; }
    else if (kind === 'logout') { b.username = 'bl' + run + 'l' + i; b.email = b.username + '@mail.com'; }
    else if (kind === 'email') b.email = 'bm' + run + 'p' + i + '@mail.com';
    return Object.assign({}, req, { body: JSON.stringify(b) });
  }

  /* Create N records (timed), then, if wanted, delete them again (timed). Returns table rows. */
  async function runWrite(w, total, conc, cleanup, tick) {
    const step = stepAt(w.create, 'POST') || STEPS.find((s) => s.path === w.create);
    const base = await C.prepare(step.id);
    if (base.error) return [{ label: 'POST ' + w.create, skipped: base.error }];
    const rows = [];
    await reserve(total);
    const created = await hammer((i) => vary(base, i, w.vary), total, conc, tick, true);
    rows.push({ label: 'POST ' + w.create, r: created });
    if (!cleanup) return rows;
    const ids = [];
    created.items.forEach((x, i) => { if (x && x.ok && x.json && x.json.id !== undefined) ids.push({ id: x.json.id, i }); });
    if (!ids.length) return rows;
    const headers = { 'Content-Type': 'application/json' };
    const delStep = STEPS.find((s) => s.method === 'DELETE' && s.path === w.del);
    let auth = null;
    if (w.login) {
      /* each account can only delete itself: log every one in first (not timed) */
      const pw = JSON.parse(base.body).password;
      await reserve(ids.length);
      const logins = await hammer((k) => ({ method: 'POST', url: C.baseUrl() + w.login, headers, expect: 200,
        body: JSON.stringify({ username: 'bm' + C.state().RUN + 'u' + ids[k].i, password: pw }) }), ids.length, conc, null, true);
      auth = logins.items.map((x) => (x && x.ok && x.json ? x.json.token : null));
    } else if (delStep && delStep.auth) {
      auth = new Array(ids.length).fill(C.state()[delStep.auth]);
    }
    const targets = [], tokens = [];
    ids.forEach((x, k) => { if (!auth || auth[k]) { targets.push(x); tokens.push(auth ? auth[k] : null); } });
    await reserve(targets.length);
    const del = await hammer((k) => ({ method: 'DELETE', url: C.baseUrl() + w.del + '?id=' + encodeURIComponent(targets[k].id),
      headers: tokens[k] ? { Authorization: 'Bearer ' + tokens[k] } : {}, expect: 200 }), targets.length, conc, null, false);
    rows.push({ label: 'DELETE ' + w.del + '  (clean-up)', r: del });
    return rows;
  }

  /* Log in N times (timed as its own row elsewhere), then log every one of those sessions out (timed). */
  async function runLogout(grp, total, conc) {
    const loginStep = STEPS.find((x) => isLogin(x) && x.path === '/clients/' + grp + '/login');
    const outPath = '/clients/' + grp + '/logout';
    if (!loginStep) return [{ label: 'POST ' + outPath, skipped: 'no login step' }];
    const createStep = STEPS.find((x) => x.method === 'POST' && x.path === '/clients/' + grp + '/create');
    if (!createStep) return [{ label: 'POST ' + outPath, skipped: 'no create step' }];
    const cbase = await C.prepare(createStep.id);
    if (cbase.error) return [{ label: 'POST ' + outPath, skipped: cbase.error }];
    /* one account per session: logging a user in several times shares one session, so only the first logout would succeed */
    await reserve(total);
    const made = await hammer((i) => vary(cbase, i, 'logout'), total, conc, null, true);
    const pw = JSON.parse(cbase.body).password;
    const run = String(C.state().RUN);
    const names = [];
    made.items.forEach((x, i) => { if (x && x.ok) names.push('bl' + run + 'l' + i); });
    if (!names.length) return [{ label: 'POST ' + outPath, skipped: 'could not create accounts' }];
    await reserve(names.length);
    const logins = await hammer((k) => ({ method: 'POST', url: C.baseUrl() + '/clients/' + grp + '/login', headers: { 'Content-Type': 'application/json' }, expect: 200,
      body: JSON.stringify({ username: names[k], password: pw }) }), names.length, conc, null, true);
    const tokens = logins.items.filter((x) => x && x.ok && x.json && x.json.token).map((x) => x.json.token);
    if (!tokens.length) return [{ label: 'POST ' + outPath, skipped: 'login returned no tokens' }];
    await reserve(tokens.length);
    const out = await hammer((k) => ({ method: 'POST', url: C.baseUrl() + outPath, headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + tokens[k] }, expect: 200 }),
      tokens.length, conc, null, false);
    return [{ label: 'POST ' + outPath + '  (write)', r: out }];
  }

  /* health, drugs, reports (one per patient) and requests, prescriptions, paid services (one per visit): create fresh parents, then one record each */
  const PARENT = { '/clinic/health/create': 'patients', '/clinic/patientdrugs/create': 'patients', '/clinic/reports/create': 'patients',
    '/clinic/requests/create': 'visits', '/clinic/prescriptions/create': 'visits', '/clinic/paidservices/create': 'visits' };
  async function runParent(step, total, conc, cleanup) {
    const pw = WRITES.find((w) => w.key === PARENT[step.path]);
    const pBase = await C.prepare(stepAt(pw.create, 'POST').id);
    if (pBase.error) return { skipped: pBase.error };
    const base = await C.prepare(step.id);
    if (base.error) return { skipped: base.error };
    await reserve(total);
    const made = await hammer((i) => vary(pBase, i, pw.vary), total, conc, null, true);
    const ids = [];
    made.items.forEach((x) => { if (x && x.ok && x.json && x.json.id !== undefined) ids.push(x.json.id); });
    if (!ids.length) return { skipped: 'could not create the parent records' };
    const b = JSON.parse(base.body);
    await reserve(ids.length);
    const r = await hammer((k) => Object.assign({}, base, { body: JSON.stringify(Object.assign({}, b, { id: ids[k] })) }), ids.length, conc, null, false);
    if (cleanup) {
      const tok = C.state().PROVIDER_TOKEN;
      await reserve(ids.length);
      await hammer((k) => ({ method: 'DELETE', url: C.baseUrl() + pw.del + '?id=' + ids[k], headers: { Authorization: 'Bearer ' + tok }, expect: 200 }), ids.length, conc, null, false);
    }
    return { r };
  }

  /* disposable accounts: create, log in, activate, suspend, then check the suspended token is refused. Suspended accounts cannot be deleted through the API. */
  async function runSuspend(g, total, conc) {
    const cs = STEPS.find((x) => x.optional && x.path === '/clients/' + g + '/create');
    const base = await C.prepare(cs.id);
    if (base.error) return { error: base.error };
    const pw = JSON.parse(base.body).password;
    const run = C.state().RUN;
    const J = { 'Content-Type': 'application/json' };
    const rows = {};
    await reserve(total);
    const created = await hammer((i) => vary(base, i, 'suspend'), total, conc, null, true);
    rows.create = created;
    const acc = [];
    created.items.forEach((x, i) => { if (x && x.ok && x.json && x.json.id !== undefined) acc.push({ id: x.json.id, user: 'bs' + run + 's' + i }); });
    if (!acc.length) return { rows };
    await reserve(acc.length);
    const logins = await hammer((k) => ({ method: 'POST', url: C.baseUrl() + '/clients/' + g + '/login', headers: J, expect: 200, body: JSON.stringify({ username: acc[k].user, password: pw }) }), acc.length, conc, null, true);
    rows.login = logins;
    const live = [];
    logins.items.forEach((x, k) => { if (x && x.ok && x.json && x.json.token) live.push({ id: acc[k].id, tok: x.json.token }); });
    const act = (what) => (k) => ({ method: 'POST', url: C.baseUrl() + '/clients/' + g + '/' + what + '?id=' + live[k].id, headers: { Authorization: 'Bearer ' + live[k].tok }, expect: 200 });
    await reserve(live.length * 3);
    rows.activate = await hammer(act('activate'), live.length, conc);
    rows.suspend = await hammer(act('suspend'), live.length, conc);
    rows.blocked = await hammer((k) => ({ method: 'POST', url: C.baseUrl() + '/clients/' + g + '/read', headers: Object.assign({ Authorization: 'Bearer ' + live[k].tok }, J), expect: 401, body: JSON.stringify({ id: live[k].id, schema: [] }) }), live.length, conc);
    return { rows };
  }

  /* ---------- UI ---------- */
  const fmt = (ms) => (ms >= 100 ? ms.toFixed(0) : ms >= 10 ? ms.toFixed(1) : ms.toFixed(2)) + ' ms';
  const sel = el('select', { id: 'vb-endpoint' });
  const inTotal = el('input', { type: 'number', min: '1', max: '100000', value: '100' });
  const inConc = el('input', { type: 'number', min: '1', max: '200', value: '10' });
  const chkWrite = el('input', { type: 'checkbox', checked: 'checked' });
  const chkClean = el('input', { type: 'checkbox', checked: 'checked' });
  const chkSusp = el('input', { type: 'checkbox' });
  const runBtn = el('button', { class: 'vc-btn primary', type: 'button', text: '▶ Run benchmark' });
  const allBtn = el('button', { class: 'vc-btn', type: 'button', text: '▶ Benchmark all endpoints' });
  const stopBtn = el('button', { class: 'vc-btn', type: 'button', text: '■ Stop', disabled: 'disabled', onclick: () => { stop = true; } });
  const status = el('div', { class: 'vb-status', text: 'Pick an endpoint and press Run, or benchmark them all. Prerequisites (login, a clinic, a patient…) are created for you.' });
  const bar = el('div', { class: 'vc-bar' });
  const results = el('div', {});

  function fillSelect() {
    const writes = chkWrite.checked;
    const keep = sel.value;
    sel.textContent = '';
    const sections = [];
    STEPS.forEach((s) => { if (!sections.includes(s.section)) sections.push(s.section); });
    sections.forEach((name) => {
      const group = STEPS.filter((s) => s.section === name && plain(s) && (writes ? true : isRead(s)));
      if (!group.length) return;
      const og = el('optgroup', { label: name });
      group.forEach((s) => og.append(el('option', { value: s.id, text: s.method + ' ' + s.path + '  —  ' + s.name })));
      sel.append(og);
    });
    if (keep && sel.querySelector('option[value="' + keep + '"]')) sel.value = keep;
  }
  chkWrite.addEventListener('change', fillSelect);

  const root = el('section', { class: 'vb-root', id: 'benchmark' },
    el('div', { class: 'vc-top' },
      el('div', {},
        el('h2', { text: 'Benchmark' }),
        el('p', { text: 'Measure how fast the server answers: latency percentiles and requests per second for one endpoint, or for all of them. Writes use unique dummy data and are deleted afterwards.' })),
      el('div', { class: 'vc-tools' },
        el('button', { class: 'vc-icon', type: 'button', title: 'Switch dark / light', text: '◐', onclick: () => C.setTheme(host.classList.contains('light') ? 'dark' : 'light') }),
        el('button', { class: 'vc-icon', type: 'button', title: 'Close (Esc)', text: '✕', onclick: () => close() }))),
    el('div', { class: 'vb-controls' },
      el('label', {}, 'Endpoint', sel),
      el('label', {}, 'Requests', inTotal),
      el('label', {}, 'Concurrency', inConc),
      el('div', { class: 'vb-btn-row' }, runBtn, allBtn, stopBtn),
      el('label', { class: 'vb-chk', title: 'Updates, and create + delete of appointments, visits, patients, services, users and providers. Creates real records.' }, chkWrite, ' Include write endpoints (creates real records)'),
      el('label', { class: 'vb-chk', title: 'Deletes the records the benchmark created (the delete is timed too) and the test accounts and records it needed. Untick to keep them.' }, chkClean, ' Delete what was written afterwards'),
      el('label', { class: 'vb-chk', title: 'Creates N disposable accounts per type and suspends them. The API cannot delete or re-activate suspended accounts, so they stay in the database.' }, chkSusp, ' Include suspend (leaves suspended accounts behind)')),
    el('div', { class: 'vc-progress' }, el('div', { class: 'vc-track' }, bar), status),
    results,
    el('div', { class: 'vb-notes' },
      el('b', { text: 'What "all" covers. ' }),
      'One row for each of the 89 test steps. Reads, searches and the one-per-patient/visit records are measured directly; updates, activate, login and the not-implemented deletes are repeated; creates make a new record with unique data on every request and the delete row is the clean-up; logout closes the sessions that were just opened. ',
      'Suspend only runs when ticked, because suspended accounts cannot be deleted or re-activated through the API. ',
      el('b', { text: 'Reading the numbers. ' }),
      'Times are measured in your browser, so they include network time, and browsers allow only a handful of parallel connections per host (about 6 over HTTP/1.1). A request is OK when it returns the status the test console expects (usually 200). ',
      el('b', { text: 'Rate limit: ' }),
      'the server\'s DoS filter allows 10 000 requests per 30 s by default (MAX_REQUESTS / PERIOD); the benchmark pauses when it gets close, so a long run waits between endpoints. The Base URL is the one set in the Test console.'));

  host.append(el('div', { class: 'vb-backdrop vc-backdrop', onclick: () => close() }), root);
  if (document.documentElement.classList.contains('vc-light') || host.classList.contains('light')) host.classList.add('light');
  fillSelect();

  const launch = el('button', { id: 'vb-launch', type: 'button', onclick: () => open() }, '⏱ Benchmark');
  document.body.append(launch);

  function open() {
    ['vc-root', 'vg-root'].forEach((i) => { const o = document.getElementById(i); if (o) o.classList.remove('open'); });
    host.classList.add('open');
    document.body.style.overflow = 'hidden';
  }
  function close() { host.classList.remove('open'); document.body.style.overflow = ''; }
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') close(); });

  function setBusy(b) {
    running = b;
    runBtn.disabled = allBtn.disabled = sel.disabled = chkWrite.disabled = chkClean.disabled = chkSusp.disabled = b;
    stopBtn.disabled = !b;
    runBtn.textContent = b ? 'Running…' : '▶ Run benchmark';
  }
  function setStatus(text, cls) { status.textContent = text; status.className = 'vb-status' + (cls ? ' ' + cls : ''); }
  const nums = () => ({
    total: Math.max(1, Math.min(100000, parseInt(inTotal.value, 10) || 100)),
    conc: Math.max(1, Math.min(200, parseInt(inConc.value, 10) || 10)),
  });
  const progress = (d, t) => { bar.style.width = (d / t) * 100 + '%'; };

  function card(label, value, bad) { return el('div', { class: 'vb-card' + (bad ? ' bad' : '') }, el('b', { text: value }), el('span', { text: label })); }

  function histogram(r) {
    const n = r.lat.length;
    if (n < 2) return null;
    const lo = r.min, hi = Math.max(r.p99, lo + 0.001), B = 24, w = (hi - lo) / B;
    const counts = new Array(B).fill(0);
    r.lat.forEach((v) => { counts[Math.min(B - 1, Math.max(0, Math.floor((v - lo) / w)))]++; });
    const peak = Math.max.apply(null, counts) || 1;
    const bars = el('div', { class: 'vb-hist' });
    counts.forEach((c, i) => bars.append(el('div', { style: 'height:' + Math.max(2, (c / peak) * 100) + '%', title: fmt(lo + i * w) + ' – ' + fmt(lo + (i + 1) * w) + ': ' + c + ' requests' })));
    return el('div', {}, el('h4', { text: 'Latency distribution (up to p99)' }), bars,
      el('div', { class: 'vb-axis' }, el('span', { text: fmt(lo) }), el('span', { text: fmt((lo + hi) / 2) }), el('span', { text: fmt(hi) })));
  }

  function showSingle(title, expect, r) {
    results.append(
      el('h3', { text: title }),
      el('div', { class: 'vb-cards' },
        card('requests / s', r.rps.toFixed(1)), card('median (p50)', fmt(r.p50)), card('p95', fmt(r.p95)), card('p99', fmt(r.p99)),
        card('OK / sent', r.ok + ' / ' + r.n, r.errors > 0), card('duration', r.secs.toFixed(2) + ' s')),
      el('table', { class: 'vb-table' },
        el('thead', {}, el('tr', {}, ['min', 'avg', 'p50', 'p90', 'p95', 'p99', 'max'].map((h) => el('th', { text: h })))),
        el('tbody', {}, el('tr', {}, [r.min, r.avg, r.p50, r.p90, r.p95, r.p99, r.max].map((v) => el('td', { text: fmt(v) }))))),
      histogram(r),
      el('p', { class: 'vb-notes', text: 'HTTP status codes: ' + Object.entries(r.codes).map(([c, n]) => c + ' × ' + n).join(', ') + '  ·  expected ' + expect + '  ·  ' + (r.bytes / 1024).toFixed(1) + ' KB received' }));
  }

  const tableHead = () => el('thead', {}, el('tr', {}, ['Endpoint', 'OK / sent', 'req/s', 'avg', 'p50', 'p95', 'p99', 'max'].map((h) => el('th', { text: h }))));
  function addRow(body, label, r, skipped) {
    const tr = el('tr', {});
    if (skipped) tr.append(el('td', { text: label }), el('td', { class: 'bad', colspan: '7', text: 'skipped: ' + skipped }));
    else tr.append(el('td', { text: label }), el('td', { class: r.errors ? 'bad' : '', text: r.ok + ' / ' + r.n }), el('td', { text: r.rps.toFixed(1) }),
      ...[r.avg, r.p50, r.p95, r.p99, r.max].map((v) => el('td', { text: fmt(v) })));
    body.append(tr);
    return !skipped && !r.errors ? 0 : 1;
  }

  async function wipe() {
    if (!chkClean.checked) return;
    setStatus('Cleaning up the test accounts and records…');
    await C.cleanup();
  }

  /* ---------- run one endpoint ---------- */
  async function runOne() {
    if (running) return;
    if (C.isBusy()) { setStatus('The test console is running; wait for it to finish.', 'bad'); return; }
    const step = STEPS.find((s) => s.id === sel.value);
    if (!step) return;
    const { total, conc } = nums();
    stop = false; setBusy(true); bar.style.width = '0'; bar.className = 'vc-bar';
    results.textContent = '';
    let wrote = false;
    try {
      const w = writeOf(step);
      if (w) {
        wrote = true;
        setStatus('Creating ' + total + ' records' + (chkClean.checked ? ' and deleting them again' : '') + '…');
        const rows = await runWrite(w, total, conc, chkClean.checked, progress);
        const body = el('tbody', {});
        results.append(el('table', { class: 'vb-table' }, tableHead(), body));
        let bad = 0;
        rows.forEach((x) => { bad += addRow(body, x.label, x.r, x.skipped); });
        rows.filter((x) => x.r).forEach((x) => { results.append(el('h3', { text: x.label }), histogram(x.r)); });
        setStatus((stop ? 'Stopped. ' : 'Done. ') + (bad ? bad + ' step(s) had errors.' : 'all requests OK.') + (chkClean.checked ? '' : ' The created records were kept.'), bad ? 'bad' : 'good');
        bar.className = 'vc-bar' + (bad ? ' bad' : '');
      } else {
        setStatus('Preparing (running prerequisites)…');
        const req = await C.prepare(step.id);
        if (req.error) { setStatus('Cannot benchmark this endpoint: ' + req.error, 'bad'); return; }
        wrote = !isRead(step);
        await reserve(total);
        setStatus('Running ' + total + ' requests, ' + conc + ' at a time…');
        const r = await hammer(() => req, total, conc, progress);
        showSingle(step.method + ' ' + step.path, req.expect, r);
        const bad = r.errors > 0;
        bar.className = 'vc-bar' + (bad ? ' bad' : '');
        const repeat = !isRead(step) && !isRepeatable(step) ? ' This endpoint is not safe to repeat on one record, so errors after the first request are expected.' : '';
        setStatus((stop ? 'Stopped. ' : 'Done. ') + r.rps.toFixed(1) + ' requests/s, median ' + fmt(r.p50) + ', ' + r.errors + ' not OK.' + repeat, bad ? 'bad' : 'good');
      }
      if (wrote) await wipe();
    } catch (e) {
      setStatus('Benchmark failed: ' + e.message, 'bad');
    } finally { setBusy(false); }
  }

  /* ---------- run everything: one row for each of the test steps ---------- */
  async function runAll() {
    if (running) return;
    if (C.isBusy()) { setStatus('The test console is running; wait for it to finish.', 'bad'); return; }
    const { total, conc } = nums();
    const withWrites = chkWrite.checked, cleanup = chkClean.checked, withSusp = chkSusp.checked;
    stop = false; setBusy(true); bar.style.width = '0'; bar.className = 'vc-bar';
    results.textContent = '';
    const body = el('tbody', {});
    const rowOf = {};
    STEPS.forEach((s) => {
      const tr = el('tr', {}, el('td', { text: s.method + ' ' + s.path + '  — ' + s.name }), el('td', { colspan: '7', text: 'waiting…' }));
      rowOf[s.id] = tr; body.append(tr);
    });
    results.append(el('table', { class: 'vb-table' }, tableHead(), body));
    const done = new Set();
    let bad = 0;
    const put = (id, r, skipped, extra) => {
      const tr = rowOf[id];
      if (!tr || done.has(id)) return;
      const label = tr.cells[0].textContent + (extra ? '  ' + extra : '');
      const t = el('tbody', {});
      bad += addRow(t, label, r, skipped);
      tr.replaceWith(t.firstChild);
      rowOf[id] = t.firstChild;
      done.add(id);
      progress(done.size, STEPS.length);
    };
    const hit = async (s, extra) => {
      if (stop) return;
      setStatus('(' + (done.size + 1) + '/' + STEPS.length + ') ' + s.method + ' ' + s.path + '  — ' + s.name);
      const req = await C.prepare(s.id);
      if (req.error) { put(s.id, null, req.error); return; }
      await reserve(total);
      put(s.id, await hammer(() => req, total, conc), null, extra);
    };
    try {
      /* A: reads, searches, updates, activate (repeated on one record) */
      for (const s of STEPS) if (isRead(s) || (withWrites && isRepeatable(s))) await hit(s, isRead(s) ? '' : '(write)');
      if (withWrites) {
        /* B: create N records with unique data, then delete them again */
        for (const w of WRITES) {
          if (stop) break;
          setStatus('(' + (done.size + 1) + '/' + STEPS.length + ') create + delete ' + w.key);
          const rows = await runWrite(w, total, conc, cleanup, null);
          const cStep = STEPS.find((x) => x.method === 'POST' && x.path === w.create && !x.optional);
          const dStep = STEPS.find((x) => x.method === 'DELETE' && x.path === w.del);
          put(cStep.id, rows[0].r, rows[0].skipped, '(write)');
          if (rows[1]) put(dStep.id, rows[1].r, rows[1].skipped, '(clean-up)');
          else put(dStep.id, null, rows[0].skipped || (cleanup ? 'nothing was created' : 'clean-up not selected'));
        }
        /* C: records that exist once per patient or visit: one fresh parent for every request */
        for (const s of STEPS) {
          if (stop || !PARENT[s.path] || s.method !== 'POST') continue;
          setStatus('(' + (done.size + 1) + '/' + STEPS.length + ') ' + s.method + ' ' + s.path + '  — one per ' + PARENT[s.path].slice(0, -1));
          const o = await runParent(s, total, conc, cleanup);
          put(s.id, o.r, o.skipped, '(write, one parent each)');
        }
        /* D: deletes the API does not implement (answer 400 by design) */
        for (const s of STEPS) if (isUnsupportedDelete(s)) await hit(s, '(not implemented, expects 400)');
        /* E: suspend: irreversible, so only when ticked */
        for (const g of GROUPS) {
          const ids = {};
          STEPS.filter((x) => x.optional && x.path.indexOf('/clients/' + g + '/') === 0).forEach((x) => {
            ids[x.path.endsWith('/create') ? 'create' : x.path.endsWith('/login') ? 'login' : x.path.endsWith('/activate') ? 'activate' : x.path.endsWith('/suspend') ? 'suspend' : 'blocked'] = x.id;
          });
          if (!withSusp) { Object.values(ids).forEach((id) => put(id, null, 'suspend not selected (it cannot be undone through the API)')); continue; }
          if (stop) break;
          setStatus('(' + (done.size + 1) + '/' + STEPS.length + ') suspend ' + g);
          const o = await runSuspend(g, total, conc);
          Object.keys(ids).forEach((k) => put(ids[k], o.rows && o.rows[k], o.error || (o.rows && o.rows[k] ? null : 'nothing to continue with'), k === 'create' ? '(write)' : ''));
        }
      }
      /* F: login (also the "fresh token" step of the clean-up) */
      for (const s of STEPS) if (isLogin(s)) await hit(s, '(login)');
      /* G: logout: N sessions are opened, then closed */
      if (withWrites) {
        for (const g of GROUPS) {
          if (stop) break;
          setStatus('(' + (done.size + 1) + '/' + STEPS.length + ') logout ' + g);
          const rows = await runLogout(g, total, conc);
          const lo = STEPS.find((x) => x.method === 'POST' && x.path === '/clients/' + g + '/logout');
          put(lo.id, rows[0].r, rows[0].skipped, '(write)');
        }
      }
      STEPS.forEach((s) => { if (!done.has(s.id)) put(s.id, null, stop ? 'stopped' : withWrites ? 'not run' : 'write endpoints not selected'); });
      bar.className = 'vc-bar' + (bad ? ' bad' : '');
      await wipe();
      const measured = STEPS.length - Array.from(body.rows).filter((r) => r.cells[1] && r.cells[1].colSpan === 7).length;
      setStatus((stop ? 'Stopped. ' : 'Done. ') + measured + ' of ' + STEPS.length + ' steps measured' + (bad ? ', ' + bad + ' with errors or skipped.' : ', all requests OK.') +
        (cleanup ? ' Test data deleted.' : ' Test data kept (use the Test console clean-up to remove it).'), bad ? 'bad' : 'good');
    } catch (e) {
      setStatus('Benchmark failed: ' + e.message, 'bad');
    } finally { setBusy(false); }
  }

  runBtn.addEventListener('click', runOne);
  allBtn.addEventListener('click', runAll);
  window.valhallaBenchmark = { open, close, run: runOne, runAll, hammer, isRead, isRepeatable, WRITES };
})();
