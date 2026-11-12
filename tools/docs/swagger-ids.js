/* Remembers the ids that responses return (provider, user, clinic, patient, visit, ...) and puts them into later requests
   made with "Try it out", so a flow works without copying ids by hand.
   Only the documentation's placeholder ids (1000, 1001, 100000) are replaced; an id you typed yourself is left alone. */
(function () {
  'use strict';
  var NAMES = { PROVIDER: 'provider', USER: 'user', CLINIC: 'clinic', PHARMACY: 'pharmacy', LAB: 'laboratory', RAD: 'radiology center',
    PATIENT: 'patient', VISIT: 'visit', APPOINTMENT: 'appointment' };
  var KEY_ENTITY = { provider_id: 'PROVIDER', admin_id: 'PROVIDER', owner_id: 'PROVIDER', user_id: 'USER', clinic_id: 'CLINIC',
    patient_id: 'PATIENT', visit_id: 'VISIT', appointment_id: 'APPOINTMENT' };
  var PLACEHOLDER = { 1000: 1, 100000: 1 };
  var ids = {};
  try { ids = JSON.parse(sessionStorage.getItem('vh.ids') || '{}') || {}; } catch (e) { ids = {}; }

  /* which resource does "id" mean on this path? */
  function entityOfPath(path) {
    var m;
    if (/\/clients\/providers\//.test(path)) return 'PROVIDER';
    if (/\/clients\/users\//.test(path)) return 'USER';
    if (/\/appointments\//.test(path)) return 'APPOINTMENT';
    if (/\/clinic\/patients\//.test(path)) return 'PATIENT';
    if (/\/clinic\/visits\//.test(path)) return 'VISIT';
    if (/\/clinic\/(health|patientdrugs|reports)\//.test(path)) return 'PATIENT';
    if (/\/clinic\/(requests|prescriptions|paidservices)\//.test(path)) return 'VISIT';
    if ((m = path.match(/\/services\/(clinics|pharmacies|laboratories|radiologycenters)\//))) {
      return { clinics: 'CLINIC', pharmacies: 'PHARMACY', laboratories: 'LAB', radiologycenters: 'RAD' }[m[1]];
    }
    return null;
  }
  var pathOf = function (url) { try { return new URL(url, location.href).pathname; } catch (e) { return String(url || ''); } };
  var isNum = function (v) { return typeof v === 'number' || (typeof v === 'string' && /^\d+$/.test(v)); };

  function save() {
    try { sessionStorage.setItem('vh.ids', JSON.stringify(ids)); } catch (e) { /* ignore */ }
    paint();
  }
  function set(entity, value) {
    if (!entity || !isNum(value)) return false;
    if (String(ids[entity]) === String(value)) return false;
    ids[entity] = Number(value);
    return true;
  }

  /* look for *_id fields in a response (a few levels deep) */
  function scan(node, depth, found) {
    if (!node || typeof node !== 'object' || depth > 3) return;
    Object.keys(node).forEach(function (k) {
      if (KEY_ENTITY[k] && isNum(node[k]) && !(k in found)) found[k] = node[k];
      else if (node[k] && typeof node[k] === 'object') scan(node[k], depth + 1, found);
    });
  }

  /* called with every Swagger response */
  function capture(res) {
    var path = pathOf(res.url);
    if (res.status < 200 || res.status >= 300 || path.indexOf('/api/') < 0) return;
    var json;
    try { json = res.body && typeof res.body === 'object' ? res.body : JSON.parse(res.text); } catch (e) { return; }
    if (!json || typeof json !== 'object') return;
    var changed = false;
    var ent = entityOfPath(path);
    if (/\/create$/.test(path) && ent && json.id !== undefined) changed = set(ent, json.id) || changed;
    if (/\/login$/.test(path) && ent && json.client_id !== undefined) changed = set(ent, json.client_id) || changed;
    var found = {};
    scan(json, 0, found);
    Object.keys(found).forEach(function (k) { if (k !== 'admin_id' && k !== 'owner_id') changed = set(KEY_ENTITY[k], found[k]) || changed; });
    if (changed) save();
  }

  /* called with every Swagger request before it is sent */
  function fill(req) {
    try {
      var path = pathOf(req.url);
      if (path.indexOf('/api/') < 0) return req;
      var used = [];
      var swap = function (obj, key, entity) {
        var r = replacement(key, obj[key], entity);
        if (r !== undefined) { obj[key] = r; used.push(key + '=' + r); }
      };
      var ent = entityOfPath(path);
      var svc = path.match(/\/services\/(clinics|pharmacies|laboratories|radiologycenters)\/staff\//);
      if (svc) ent = { clinics: 'CLINIC', pharmacies: 'PHARMACY', laboratories: 'LAB', radiologycenters: 'RAD' }[svc[1]];
      if (req.body && typeof req.body === 'string' && req.body.charAt(0) === '{') {
        var b = JSON.parse(req.body);
        Object.keys(b).forEach(function (k) {
          if (KEY_ENTITY[k]) swap(b, k, KEY_ENTITY[k]);
          else if (k === 'id') swap(b, k, ent);
          else if (k === 'entity_id' && svc) swap(b, k, ent);
        });
        if (used.length) req.body = JSON.stringify(b);
      }
      var u = new URL(req.url, location.href);
      var q = {};
      u.searchParams.forEach(function (v, k) { q[k] = v; });
      Object.keys(q).forEach(function (k) {
        var before = used.length;
        if (KEY_ENTITY[k]) swap(q, k, KEY_ENTITY[k]);
        else if (k === 'id') swap(q, k, ent);
        if (used.length > before) u.searchParams.set(k, q[k]);
      });
      if (used.length) {
        req.url = u.toString();
        if (window.vhToast) window.vhToast('Used ids from earlier responses: ' + used.join(', '));
      }
    } catch (e) { /* never block a request */ }
    return req;
  }

  /* the new value for body/query field `key` currently holding `val`, or undefined to leave it */
  function replacement(key, val, entity) {
    var ent = KEY_ENTITY[key] || (key === 'id' || key === 'entity_id' ? entity : null);
    if (!ent || ids[ent] === undefined || !isNum(val) || !PLACEHOLDER[Number(val)]) return undefined;
    return typeof val === 'string' ? String(ids[ent]) : ids[ent];
  }
  var sig = function () { return JSON.stringify(ids); };
  var filled = typeof WeakMap === 'function' ? new WeakMap() : null;
  function setNative(input, value) {
    var proto = input.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    Object.getOwnPropertyDescriptor(proto, 'value').set.call(input, value);
    input.dispatchEvent(new Event('input', { bubbles: true }));
  }

  /* Put the saved ids into the editable body and parameter boxes of every operation that has "Try it out" open.
     Runs when the editor first appears and whenever the saved ids change; what the user types in between is kept. */
  function prefill() {
    var now = sig();
    if (now === '{}') return;
    document.querySelectorAll('#swagger-ui .opblock').forEach(function (block) {
      var pathEl = block.querySelector('.opblock-summary-path');
      var path = pathEl ? (pathEl.getAttribute('data-path') || pathEl.textContent) : '';
      var ent = entityOfPath(path);
      var ta = block.querySelector('textarea.body-param__text');
      if (ta && (!filled || filled.get(ta) !== now)) {
        if (filled) filled.set(ta, now);
        try {
          var b = JSON.parse(ta.value), hit = false;
          Object.keys(b).forEach(function (k) { var r = replacement(k, b[k], ent); if (r !== undefined) { b[k] = r; hit = true; } });
          if (hit) setNative(ta, JSON.stringify(b, null, 2));
        } catch (e) { /* not a JSON object: leave it */ }
      }
      block.querySelectorAll('table.parameters tr[data-param-name]').forEach(function (tr) {
        var input = tr.querySelector('input[type=text], input:not([type])');
        if (!input || input.disabled || (filled && filled.get(input) === now)) return;
        if (filled) filled.set(input, now);
        var r = replacement(tr.getAttribute('data-param-name'), input.value, ent);
        if (r !== undefined) setNative(input, String(r));
      });
    });
  }
  var timer;
  function schedule() { clearTimeout(timer); timer = setTimeout(prefill, 60); }
  document.addEventListener('DOMContentLoaded', function () {
    new MutationObserver(schedule).observe(document.body, { childList: true, subtree: true });
  });

  function paint() {
    schedule();
    var box = document.getElementById('vh-ids');
    if (!box) return;
    box.textContent = '';
    var keys = Object.keys(ids).filter(function (k) { return NAMES[k]; });
    if (!keys.length) { box.style.display = 'none'; return; }
    box.style.display = '';
    box.title = 'Ids returned by earlier responses; Try it out uses them in place of the example ids (1000, 100000).';
    keys.forEach(function (k) {
      var c = document.createElement('span');
      c.className = 'vh-chip';
      c.textContent = NAMES[k] + ' #' + ids[k];
      box.appendChild(c);
    });
    var x = document.createElement('button');
    x.type = 'button'; x.className = 'vh-chipx'; x.textContent = 'clear ids';
    x.onclick = function () { ids = {}; save(); };
    box.appendChild(x);
  }
  document.addEventListener('DOMContentLoaded', paint);
  window.vhIds = { capture: capture, fill: fill, get: function () { return ids; }, clear: function () { ids = {}; save(); } };
})();
