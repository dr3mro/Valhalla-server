(function () {
  'use strict';
  const host = document.getElementById('vg-root');
  if (!host) return;
  const content = host.querySelector('.vg-content');
  const nav = host.querySelector('.vg-nav');
  const links = [];
  // scroll only the panel's own container (scrollIntoView would also scroll the page behind it)
  function goto(h, smooth) {
    const top = h.getBoundingClientRect().top - content.getBoundingClientRect().top + content.scrollTop - 8;
    content.scrollTo({ top: top, behavior: smooth ? 'smooth' : 'auto' });
  }
  const search = document.createElement('input'); search.placeholder = 'Filter sections...'; nav.append(search);
  content.querySelectorAll('h2').forEach((h) => {
    const a = document.createElement('a'); a.href = '#' + h.id; a.textContent = h.textContent;
    a.addEventListener('click', (e) => { e.preventDefault(); e.stopPropagation(); goto(h, true); });
    nav.append(a); links.push([a, h]);
  });
  search.addEventListener('input', () => { const q = search.value.toLowerCase(); links.forEach(([a]) => { a.style.display = a.textContent.toLowerCase().includes(q) ? '' : 'none'; }); });
  content.addEventListener('scroll', () => {
    let cur = links[0];
    links.forEach((p) => { if (p[1].offsetTop - content.offsetTop - 40 <= content.scrollTop) cur = p; });
    links.forEach(([a]) => a.classList.toggle('on', cur && a === cur[0]));
  });
  content.querySelectorAll('pre').forEach((pre) => {
    const b = document.createElement('button'); b.className = 'vg-copy'; b.type = 'button'; b.textContent = 'Copy';
    b.addEventListener('click', () => { const t = pre.querySelector('code').innerText;
      (navigator.clipboard ? navigator.clipboard.writeText(t) : Promise.reject()).then(() => { b.textContent = 'Copied'; setTimeout(() => { b.textContent = 'Copy'; }, 1200); }, () => {}); });
    pre.append(b);
  });
  function setTheme(t) {
    document.querySelectorAll('#vc-root,#vg-root,#vb-root').forEach((r) => r.classList.toggle('light', t === 'light'));
    document.documentElement.classList.toggle('vc-light', t === 'light');
    try { localStorage.setItem('vc.theme', t); } catch (e) { /* ignore */ }
  }
  let dark = false; try { dark = localStorage.getItem('vc.theme') === 'dark'; } catch (e) { /* ignore */ }
  if (!dark) { host.classList.add('light'); document.documentElement.classList.add('vc-light'); } // light is the default
  function open(section) {
    ['vc-root', 'vb-root'].forEach((i) => { const o = document.getElementById(i); if (o) o.classList.remove('open'); });
    host.classList.add('open'); document.body.style.overflow = 'hidden';
    if (section) { const h = content.querySelector('#' + section); if (h) goto(h, false); }
  }
  function close() { host.classList.remove('open'); document.body.style.overflow = ''; }
  host.querySelector('.vg-close').addEventListener('click', close);
  host.querySelector('.vg-theme').addEventListener('click', () => setTheme(host.classList.contains('light') ? 'dark' : 'light'));
  host.querySelector('.vg-backdrop').addEventListener('click', close);
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') close(); });
  document.getElementById('vg-launch').addEventListener('click', () => open());
  document.addEventListener('click', (e) => { const a = e.target.closest && e.target.closest('a[href^="#vg:"]'); if (a) { e.preventDefault(); e.stopPropagation(); open(a.getAttribute('href').slice(4)); } }, true);
  host.querySelectorAll('.vg-dl').forEach((btn) => btn.addEventListener('click', () => {
    const k = btn.getAttribute('data-kind');
    const data = k === 'postman' ? window.__VG_POSTMAN__ : window.__VG_SPEC__;
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }));
    a.download = k === 'postman' ? 'valhalla.postman_collection.json' : 'valhalla.openapi.json';
    document.body.append(a); a.click(); a.remove();
  }));
  const ht = document.getElementById('vh-theme'); if (ht) ht.addEventListener('click', () => setTheme(host.classList.contains('light') ? 'dark' : 'light'));
  window.valhallaGuide = { open, close };
})();
