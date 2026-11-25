// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 R5OS Contributors
// Aomi window mockups (25 Nov 2026). One builder, three layouts: document.body.dataset.option = a | b | c.
// Icons are stand-ins; the real window uses Haiku icons (r5_icon_bitmap, r5_vector_icon_bitmap).

const OPT = document.body.dataset.option;
const S = (p, extra = '') => `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" ${extra}>${p}</svg>`;
const I = {
  back: S('<path d="M15 5l-7 7 7 7"/>'),
  fwd: S('<path d="M9 5l7 7-7 7"/>'),
  reload: S('<path d="M20 11a8 8 0 1 0-2.3 5.7"/><path d="M20 4v7h-7"/>'),
  stop: S('<path d="M6 6l12 12M18 6L6 18"/>'),
  home: S('<path d="M4 11l8-7 8 7"/><path d="M6 10v10h12V10"/>'),
  lock: S('<rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>', 'style="width:15px;height:15px;color:#1b7f45"'),
  star: S('<path d="M12 3l2.7 5.6 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.9 1-6.1L3.2 9.5l6.1-.9z"/>', 'style="width:17px;height:17px;margin-left:auto;opacity:.6"'),
  dl: S('<path d="M12 4v11M7 10l5 5 5-5M5 20h14"/>'),
  menu: S('<path d="M4 7h16M4 12h16M4 17h16"/>'),
  search: S('<circle cx="11" cy="11" r="6"/><path d="M20 20l-4.5-4.5"/>', 'style="width:18px;height:18px"'),
  mask: S('<path d="M3 10c3-1 6-1 9 0s6 1 9 0c0 4-2 7-5 7-2 0-3-2-4-2s-2 2-4 2c-3 0-5-3-5-7z"/><circle cx="8.5" cy="12" r="1"/><circle cx="15.5" cy="12" r="1"/>', 'style="width:15px;height:15px"'),
};
const MARK = `<svg class="mark" viewBox="0 0 64 64"><defs><linearGradient id="sea" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7cc0ff"/><stop offset="1" stop-color="#3898ff"/></linearGradient></defs>
<circle cx="32" cy="32" r="30" fill="#0f2a4a" stroke="#fff" stroke-opacity=".35" stroke-width="1.5"/><path d="M2.3 36 Q32 24 61.7 36 A30 30 0 0 1 2.3 36Z" fill="url(#sea)"/>
<path d="M9 34 Q32 23 55 34" stroke="#fff" stroke-width="3" fill="none" stroke-linecap="round"/><circle cx="32" cy="20" r="5" fill="#fff"/></svg>`;
const fav = (c, l) => `<span class="fav" style="background:${c};color:#fff;font-size:9px;font-weight:800;display:inline-flex;align-items:center;justify-content:center">${l}</span>`;

const TABS = [
  {t: 'The quiet return of the desktop – Northline', f: fav('#b45309', 'N')},
  {t: 'R5 Operating System', f: fav('#0084ff', 'R5')},
  {t: 'Servo – the embeddable web engine', f: fav('#1f2937', 'S')},
];

// --- pieces ---
const menubar = (open) => `<div class="menubar">${['Window', 'Edit', 'View', 'History', 'Bookmarks', 'Help']
  .map(m => `<span class="${m === open ? 'open' : ''}">${m}</span>`).join('')}</div>`;
function tabs(list, on) {
  return `<div class="tabs">${list.map((x, i) => `<div class="t ${i === on ? 'on' : ''}">${x.f}<span class="ti">${x.t}</span><span class="x">×</span></div>`).join('')}<div class="plus">+</div></div>`;
}
function url(kind) {
  if (kind !== 'page') return `<div class="url focus">${I.search}<span class="ph">Search or type an address</span></div>`;
  return `<div class="url">${I.lock}<span><span class="host">northline.example</span><span class="rest">/essays/quiet-return-of-the-desktop</span></span>${OPT === 'b' ? I.star : ''}</div>`;
}
function nav({loading = true, kind = 'page', priv = false, dlOn = false, menuOn = false} = {}) {
  const back = `<div class="ib box">${I.back}</div>`, fwd = `<div class="ib box dim">${I.fwd}</div>`;
  const rl = `<div class="ib box">${loading ? I.stop : I.reload}</div>`;
  const pill = priv ? `<span class="pill">${I.mask}Private</span>` : '';
  const prog = loading && kind === 'page' ? '<div class="progress"></div>' : '';
  if (OPT === 'a') return `<div class="nav">${back}${fwd}${rl}<div class="ib box">${I.home}</div>${url(kind)}<span class="btn">Go</span>${pill}${prog}</div>`;
  if (OPT === 'b') return `<div class="nav">${back}${fwd}${rl}${url(kind)}${pill}<div class="ib ${dlOn ? 'on' : ''}">${I.dl}</div><div class="ib ${menuOn ? 'on' : ''}">${I.menu}</div>${prog}</div>`;
  return `<div class="nav">${back}${fwd}${rl}${url(kind)}${pill}<div class="ib">${I.star}</div>${prog}</div>`;
}
const bmarks = () => `<div class="bmarks"><span>${fav('#0084ff', 'R5')}R5</span><span>${fav('#1f2937', 'S')}Servo</span><span>${fav('#b45309', 'N')}Northline</span><span>${fav('#16a34a', 'W')}Wikipedia</span><span>${fav('#7c3aed', 'H')}HaikuPorts</span><span style="margin-left:auto;color:var(--sub)">Other bookmarks ▸</span></div>`;
const status = () => `<div class="status"><span>Loading northline.example — 34 of 55 items</span><div class="sb"><i></i></div></div>`;

const SITE = `<div class="site"><header><b>Northline</b><span>Essays</span><span>Notes</span><span>About</span></header>
<article><h1>The quiet return of the desktop</h1><div class="meta">25 November 2026 · 9 min read</div>
<p>For a decade the browser grew into a second operating system. Every app became a tab, and every tab carried its own buttons, menus and colours. Somewhere along the way the desktop itself went quiet.</p>
<div class="fig"></div>
<p>Small systems remember a different idea: one window, one job, and the system around it doing the rest. Files are files, menus are menus, and the page is simply the page.</p>
<p>That idea is not nostalgia. It is a way to keep the frame calm so the work inside it can be loud.</p></article></div>`;

function ntp(priv) {
  const tiles = [['#0084ff', 'R5', 'R5'], ['#1f2937', 'S', 'Servo'], ['#b45309', 'N', 'Northline'], ['#16a34a', 'W', 'Wikipedia'], ['#7c3aed', 'H', 'HaikuPorts'], ['#0f766e', 'G', 'GitHub']]
    .map(([c, l, n]) => `<div class="tile"><i style="background:${c}">${l}</i>${n}</div>`).join('');
  if (priv) return `<div class="ntp private">${MARK}<h2>Private window</h2><div class="search">${I.search}Search privately or type an address</div>
<div class="pnote">Aomi forgets the history, cookies and site data of this window when you close it. Downloads and new bookmarks stay.</div></div>`;
  return `<div class="ntp">${MARK}<h2>Aomi</h2><div class="search">${I.search}Search the web or type an address</div><div class="tiles">${tiles}</div>
<div class="recent"><b>Recently closed</b>Servo 0.8.0 release notes · R5 Book – Window effects</div></div>`;
}

function sideList(dlOpen, priv) {
  if (priv) return `<div class="side"><div class="row newt">+ <span class="ti">New tab</span></div><div class="row on">${fav("#4c1d95", "")}<span class="ti">Private – New tab</span><span class="x">×</span></div><div class="cat">Bookmarks</div><div class="cat">Downloads</div></div>`;
  const rows = TABS.map((x, i) => `<div class="row ${i === 0 ? 'on' : ''}">${x.f}<span class="ti">${x.t}</span><span class="x">×</span></div>`).join('');
  const more = [['#16a34a', 'W', 'Desktop environment – Wikipedia'], ['#7c3aed', 'H', 'HaikuPorts – recipes']]
    .map(([c, l, t]) => `<div class="row">${fav(c, l)}<span class="ti">${t}</span><span class="x">×</span></div>`).join('');
  const dls = dlOpen ? `<div class="cat open">Downloads</div>${dlItems(true)}` : `<div class="cat">Downloads</div>`;
  return `<div class="side"><div class="row newt">+ <span class="ti">New tab</span></div>${rows}${more}
<div class="cat">Bookmarks</div>${dls}</div>`;
}
function dlItems(small) {
  const items = [['servo-0.8.0-src.tar.gz', '182 of 410 MB · 2 minutes left', 44], ['r5-wallpapers.zip', '24 MB · Done', 100], ['quiet-desktop.pdf', '1.2 MB · Done', 100]];
  return items.map(([n, s, p]) => `<div class="dl ${small ? 'in' : ''}" ${small ? 'style="padding:5px 4px;gap:8px"' : ''}><div class="doc" ${small ? 'style="width:18px;height:22px"' : ''}></div>
<div class="grow"><div class="nm" style="overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${n}</div><div class="sm">${s}</div>${p < 100 ? `<div class="sb"><i style="width:${p}%"></i></div>` : ''}</div></div>`).join('');
}

// --- the window ---
function browser({title, kind = 'page', priv = false, dlOn = false, menuOn = false}) {
  const view = kind === 'page' ? SITE + (OPT === 'b' ? '<div class="hoverlink">https://northline.example/notes/</div>' : '') : ntp(priv);
  const tl = kind === 'page' ? TABS : [{t: priv ? 'Private – New tab' : 'New tab', f: fav(priv ? '#4c1d95' : '#3898ff', '')}, ...(priv ? [] : TABS.slice(1))];
  const loading = kind === 'page';
  let body = '';
  if (OPT === 'a') body = menubar() + tabs(tl, 0) + nav({loading, kind, priv}) + bmarks() + `<div class="main"><div class="view">${view}</div></div>` + status();
  if (OPT === 'b') body = tabs(tl, 0) + nav({loading, kind, priv, dlOn, menuOn}) + `<div class="main"><div class="view">${view}</div></div>`;
  if (OPT === 'c') body = menubar() + nav({loading, kind, priv}) + `<div class="main">${sideList(dlOn, priv)}<div class="view">${view}</div></div>`;
  return win(title, 14, 8, 1252, 784, body);
}
const win = (title, x, y, w, h, body, extra = '') =>
  `<div class="win" style="left:${x}px;top:${y}px;width:${w}px;height:${h}px;${extra}"><div class="wtab"><span class="box"></span>${title}<span class="box zoom"></span></div><div class="wbody">${body}</div></div>`;

function settingsWin() {
  const cb = (on, t) => `<span class="cb"><i class="${on ? 'on' : ''}"></i>${t}</span>`;
  const form = `<div class="form">
<label>Start with:</label><span><span class="mf">New tab page</span></span>
<label>Home page:</label><span class="f">https://r5-operating-system.github.io/</span>
<label>Search engine:</label><span><span class="mf">DuckDuckGo</span></span>
<label>Search address:</label><span class="f">https://duckduckgo.com/?q=%s</span>
<label></label>${cb(OPT === 'a', 'Show home button')}
<label></label>${cb(OPT !== 'b', 'Show bookmarks bar')}
<label></label>${cb(false, 'Zoom text only')}
<div class="boxtitle">Downloads</div>
<label>Save downloads to:</label><span style="display:flex;gap:8px"><span class="f" style="flex:1">/boot/home/Desktop</span><span class="btn">Choose…</span></span>
<label></label>${cb(false, 'Ask where to save each file')}
<div class="boxtitle">Private windows</div>
<label></label><span class="note">A private window forgets its history, cookies and site data when you close it.</span>
</div><div class="wfoot"><span class="btn">Revert</span><span class="btn">Defaults</span></div>`;
  return win('Aomi settings', 360, 120, 560, 560, form, 'z-index:4');
}
function dlWin() {
  return win('Downloads', 820, 300, 420, 300, `<div style="padding:8px 10px;flex:1">${dlItems(false)}</div>
<div class="wfoot"><span class="btn">Remove finished</span><span class="btn">Open downloads folder</span></div>`, 'z-index:4');
}
function bMenu() {
  return `<div class="menu" style="right:24px;top:76px;width:270px">
<div>New tab<kbd>⌘T</kbd></div><div>New window<kbd>⌘N</kbd></div><div>New private window<kbd>⇧⌘N</kbd></div><hr>
<div class="sub">History</div><div class="sub hi">Bookmarks</div><div>Downloads<kbd>⌘D</kbd></div><hr>
<div class="zoomrow">Zoom<span style="margin-left:auto"></span><span class="btn">−</span><span class="btn">100 %</span><span class="btn">+</span></div>
<div>Find in page…<kbd>⌘F</kbd></div><div>Save page as…<kbd>⌘S</kbd></div><div>Print…<kbd>⌘P</kbd></div><div>View source<kbd>⌘U</kbd></div><hr>
<div>Show menu bar</div><div>Settings…<kbd>⌘,</kbd></div><div>About Aomi</div><hr><div>Quit<kbd>⌘Q</kbd></div></div>
<div class="menu" style="right:296px;top:186px;width:240px"><div>Bookmark this page<kbd>⌘B</kbd></div><div>Manage bookmarks</div><div>Show bookmarks bar</div><hr>
<div>${fav('#0084ff', 'R5')}R5</div><div>${fav('#1f2937', 'S')}Servo</div><div>${fav('#b45309', 'N')}Northline</div><div class="sub">Other bookmarks</div></div>`;
}
function bPanel() {
  return `<div class="panel" style="right:56px;top:76px"><h4>Downloads</h4>${dlItems(false)}<div class="foot"><span class="btn">Show all downloads</span><span class="btn">Open folder</span></div></div>`;
}
const callout = (x, y, t) => `<div class="callout" style="left:${x}px;top:${y}px">${t}</div>`;

// --- scenes ---
const NOTES = {
  a: {page: [[900, 120, 'Menu bar, tabs, navigation row, bookmarks bar and the status line: about 130 px before the page. Everything is visible, as in WebPositive.']],
      downloads: [[820, 230, 'Downloads is its own window (WebPositive DownloadWindow, MIT copy), Command-D.']]},
  b: {page: [[900, 130, 'One row under the tabs: about 76 px before the page. The link under the mouse shows at the bottom left; a thin bar under the toolbar shows the progress.']],
      menu: [[430, 100, 'One menu button: the same Haiku menus as submenus, every shortcut kept. "Show menu bar" brings the menu bar of option A back.']],
      downloads: [[500, 110, 'A panel under the downloads button. "Show all downloads" opens the Downloads window.']]},
  c: {page: [[900, 130, 'Tabs in a list on the left, 200 wide, like the Settings list. Bookmarks and Downloads fold under the tabs.']],
      downloads: [[230, 520, 'Downloads unfolded in the list. The fold works as in Settings: one open at a time.']]},
};
const SCENES = {
  page: () => browser({title: 'Northline – Aomi'}),
  newtab: () => browser({title: 'New tab – Aomi', kind: 'newtab'}),
  private: () => browser({title: 'Private – Aomi', kind: 'newtab', priv: true}),
  downloads: () => browser({title: 'Northline – Aomi', dlOn: true}) + (OPT === 'a' ? dlWin() : OPT === 'b' ? bPanel() : ''),
  menu: () => browser({title: 'Northline – Aomi', menuOn: true}) + bMenu(),
  settings: () => browser({title: 'Northline – Aomi'}) + settingsWin(),
};
const LIST = ['page', 'newtab', 'private', 'downloads', ...(OPT === 'b' ? ['menu'] : []), 'settings'];
const LABEL = {page: 'Page', newtab: 'New tab page', private: 'Private window', downloads: 'Downloads', menu: 'Menu button', settings: 'Settings form'};

const st = {};
function fromHash() {
  Object.assign(st, {scene: 'page', dark: false, glass: true, notes: true});
  location.hash.slice(1).split('-').forEach(p => {
    if (LIST.includes(p)) st.scene = p; if (p === 'dark') st.dark = true; if (p === 'flat') st.glass = false; if (p === 'clean') st.notes = false;
  });
}
fromHash();
addEventListener('hashchange', () => { fromHash(); render(); });
function render() {
  const d = document.getElementById('desk');
  d.className = `desk ${st.dark ? 'dark' : 'light'} ${st.glass ? 'glass' : 'flat'} ${st.notes ? '' : 'callouts-off'}`;
  d.innerHTML = SCENES[st.scene]() + ((NOTES[OPT][st.scene] || []).map(n => callout(...n)).join(''));
  document.querySelectorAll('[data-scene]').forEach(b => b.setAttribute('aria-pressed', b.dataset.scene === st.scene));
  document.querySelectorAll('[data-k]').forEach(b => b.setAttribute('aria-pressed', String(st[b.dataset.k] === (b.dataset.v === '1'))));
}
document.getElementById('scenes').innerHTML = LIST.map(s => `<button type="button" data-scene="${s}">${LABEL[s]}</button>`).join('');
document.addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  if (b.dataset.scene) st.scene = b.dataset.scene;
  if (b.dataset.k) st[b.dataset.k] = b.dataset.v === '1';
  render();
});
render();
