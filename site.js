// The page's own motion and the small behaviours around it (site.css holds the styles).
document.documentElement.classList.add('hc-ready');
const reduce = () => window.matchMedia('(prefers-reduced-motion: reduce)').matches;
const EASE = 'cubic-bezier(.2, .8, .2, 1)';        // --ease, the kit's UI ease-out
const TRAVEL = 'cubic-bezier(.65, 0, .35, 1)';     // --m-ease-io, travel on screen

/* ---------- picture groups: reveal once, as they come into view ---------- */
const groups = [...document.querySelectorAll('[data-reveal]')];
if (groups.length && 'IntersectionObserver' in window) {
  const io = new IntersectionObserver((entries) => {
    for (const e of entries) if (e.isIntersecting) { e.target.classList.add('is-in'); io.unobserve(e.target); }
  }, { rootMargin: '0px 0px -12% 0px' });
  groups.forEach((g) => io.observe(g));
} else {
  groups.forEach((g) => g.classList.add('is-in'));
}

/* ---------- trailer: one big play disc over the poster ---------- */
const video = document.querySelector('#trailer video');
const play = document.querySelector('.trailer__play');
if (video && play) {
  video.removeAttribute('controls');
  play.hidden = false;
  play.addEventListener('click', () => {
    video.controls = true;
    play.hidden = true;
    video.play().catch(() => { play.hidden = false; });
  });
  video.addEventListener('play', () => { play.hidden = true; video.controls = true; });
  video.addEventListener('ended', () => { play.hidden = false; video.controls = false; video.load(); });
}

/* ---------- screenshots: open large, grown out of the thumbnail ---------- */
const box = document.createElement('dialog');
box.className = 'lightbox';
box.setAttribute('aria-label', 'Screenshot');
box.innerHTML = '<img class="lightbox__img" alt=""><button class="lightbox__close" type="button" aria-label="Close"><svg viewBox="0 0 18 18" aria-hidden="true"><path d="M4 4l10 10M14 4 4 14" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg></button>';
document.body.append(box);
const big = box.querySelector('.lightbox__img');
let from = null;      // the thumbnail the open picture came from
let closing = false;

// the transform that lays the big picture over the thumbnail (both centred, matched by width)
const overThumb = () => {
  const t = from.getBoundingClientRect(), b = big.getBoundingClientRect();
  const s = t.width / b.width;
  return `translate(${t.left - b.left + (t.width - b.width * s) / 2}px, ${t.top - b.top + (t.height - b.height * s) / 2}px) scale(${s})`;
};

async function open(link) {
  from = link.querySelector('img');
  big.src = link.href;
  big.alt = from.alt;
  closing = false;
  box.showModal();
  try { await big.decode(); } catch { /* shown as it loads */ }
  if (reduce()) {
    box.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 150, easing: EASE });
    return;
  }
  big.animate([{ transform: overThumb(), opacity: .6 }, { transform: 'none', opacity: 1 }], { duration: 340, easing: TRAVEL });
  box.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 240, easing: EASE, pseudoElement: '::backdrop' });
}

async function close() {
  if (closing || !box.open) return;
  closing = true;
  if (reduce() || !from) {
    await box.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 120, easing: EASE }).finished.catch(() => {});
  } else {
    box.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 260, easing: EASE, pseudoElement: '::backdrop', fill: 'forwards' });
    await big.animate([{ transform: 'none', opacity: 1 }, { transform: overThumb(), opacity: .6 }], { duration: 280, easing: TRAVEL, fill: 'forwards' }).finished.catch(() => {});
  }
  box.close();
  big.getAnimations().forEach((a) => a.cancel());
  box.getAnimations({ subtree: false }).forEach((a) => a.cancel());
  closing = false;
  from?.closest('a')?.focus({ preventScroll: true });
}

document.addEventListener('click', (e) => {
  const link = e.target.closest('a.shot-link');
  if (!link || e.metaKey || e.ctrlKey || e.shiftKey || e.button !== 0) return;
  e.preventDefault();
  open(link);
});
box.addEventListener('cancel', (e) => { e.preventDefault(); close(); });
box.addEventListener('click', (e) => { if (e.target === box || e.target.closest('.lightbox__close')) close(); });

/* ---------- the five newest releases from GitHub, faded in over the list as written ---------- */
const list = document.querySelector('[data-releases]');
if (list) {
  const count = list.children.length;
  // GitHub allows 60 unauthenticated requests an hour per visitor: keep the trimmed list for 15 minutes
  const KEY = 'hc-releases', AGE = 15 * 60 * 1000;
  const cached = () => { try { const c = JSON.parse(localStorage.getItem(KEY)); return c && Date.now() - c.t < AGE ? c.list : null; } catch { return null; } };
  const load = () => fetch('https://api.github.com/repos/mniklas189-png/how-could-it-be-you/releases?per_page=100', { headers: { Accept: 'application/vnd.github+json' } })
    .then((r) => (r.ok ? r.json() : Promise.reject(r.status)))
    .then((all) => {
      const trimmed = all.filter((r) => !r.draft).map((r) => {
        const en = (r.body || '').match(/^##\s*English\s*[–-]\s*(.+)$/m);
        return { tag: r.tag_name, url: r.html_url, date: r.published_at, pre: r.prerelease, title: en ? en[1] : '' };
      });
      try { localStorage.setItem(KEY, JSON.stringify({ t: Date.now(), list: trimmed })); } catch { /* private mode */ }
      return trimmed;
    });
  const have = cached();
  (have ? Promise.resolve(have) : load())
    .then((published) => {
      const total = document.querySelector('[data-release-count]');
      if (total && published.length >= 30 && published.length < 100) total.textContent = String(published.length);
      const rel = published.filter((r) => !r.pre).slice(0, count);
      if (rel.length < count) return;
      const day = new Intl.DateTimeFormat('en-GB', { day: 'numeric', month: 'short', year: 'numeric', timeZone: 'UTC' });
      const rows = rel.map((r) => {
        let line = r.title.replace(/^Version\s+[\d.]+:\s*/i, '').trim();
        if (!line) return null;
        line = line.charAt(0).toUpperCase() + line.slice(1);
        if (!/[.!?]$/.test(line)) line += '.';
        const li = document.createElement('li');
        const name = document.createElement('span'); name.className = 'rows__name';
        const a = document.createElement('a'); a.href = r.url; a.textContent = 'Version ' + r.tag.replace(/^v/, '');
        name.append(a);
        const p = document.createElement('p'); p.textContent = line;
        const when = document.createElement('span'); when.className = 'rows__value'; when.textContent = day.format(new Date(r.date));
        li.append(name, p, when);
        return li;
      });
      if (!rows.every(Boolean)) return;
      if (rows.map((li) => li.textContent).join('|') === [...list.children].map((li) => li.textContent).join('|')) return;
      list.replaceChildren(...rows);
      list.animate([{ opacity: .35 }, { opacity: 1 }], { duration: 240, easing: EASE });
    })
    .catch(() => {});
}
