// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HtmlReport.h"

#include "Report.h"
// std
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

namespace {

// Inline stylesheet. Neutral light theme, self-contained: system font stack
// (no @font-face), no external resources, so the document stays portable
// offline and when embedded. Accent is a generic blue; green/red/grey carry the
// pass/fail/skip semantics.
constexpr const char *kStyle = R"CSS(
:root {
  color-scheme: light;
  --bg: #eef1f4; --fg: #1b2127; --card: #ffffff; --panel: #f6f8fa;
  --border: #e3e8ec; --border-soft: #edf0f3; --thead: #f3f6f8;
  --muted: #5f6b74; --muted2: #66727b; --muted3: #7a848d; --dark: #333c44;
  --accent: #487a90; --accent-hover: #3a6478;
  --pass: #1a7f37; --fail: #cf222e; --skip: #6e7781; --link: #487a90;
  --mono: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}
* { box-sizing: border-box; }
[hidden] { display: none !important; } /* beat element display rules (e.g. flex) */
html, body {
  margin: 0; background: var(--bg); color: var(--fg);
  font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  -webkit-font-smoothing: antialiased;
}
img { display: block; }
a { color: var(--link); text-decoration: none; }
a:hover { color: var(--accent-hover); text-decoration: underline; }
.kmono { font-family: var(--mono); }
.wrap { max-width: 1200px; margin: 0 auto; padding: 0 32px 90px; }

/* header */
header {
  display: flex; align-items: center; justify-content: space-between; gap: 24px;
  padding: 26px 0 22px; border-bottom: 1px solid var(--border); flex-wrap: wrap;
}
.brand { display: flex; align-items: center; gap: 14px; }
.logo { height: 30px; width: auto; color: var(--accent); flex: none; }
.title { font-size: 19px; font-weight: 700; letter-spacing: -.01em; }
.subtitle { font-size: 13px; color: var(--muted); }
.runbox { font-size: 12px; color: var(--muted2); text-transform: uppercase;
  letter-spacing: .06em; text-align: right; }

/* summary */
.summary {
  display: grid; grid-template-columns: 300px 1fr; gap: 24px; margin-top: 26px;
  align-items: stretch;
}
.ring-card {
  background: linear-gradient(180deg, #fff, #f4f7f9); border: 1px solid var(--border);
  border-radius: 14px; padding: 22px 24px; display: flex; align-items: center;
  gap: 22px;
}
.ring { position: relative; width: 104px; height: 104px; flex: none;
  border-radius: 50%; display: flex; align-items: center; justify-content: center; }
.ring .hole {
  width: 78px; height: 78px; border-radius: 50%; background: #fff; display: flex;
  flex-direction: column; align-items: center; justify-content: center;
}
.ring .pct { font-size: 26px; font-weight: 700; line-height: 1; }
.ring .pct span { font-size: 14px; color: var(--muted); }
.ring .cap { font-size: 10.5px; color: var(--muted); text-transform: uppercase;
  letter-spacing: .05em; margin-top: 3px; }
.legend { display: flex; flex-direction: column; gap: 9px; }
.legend .row { display: flex; align-items: center; gap: 8px; font-size: 13px;
  color: var(--dark); }
.legend .dot { width: 9px; height: 9px; border-radius: 2px; }
.legend .n { margin-left: auto; font-weight: 600; }
.stats { display: grid; grid-template-columns: repeat(4, 1fr); gap: 14px; }
.stat {
  background: var(--card); border: 1px solid var(--border); border-radius: 12px;
  padding: 16px 18px; display: flex; flex-direction: column; gap: 6px;
}
.stat .k { font-size: 11px; color: var(--accent); text-transform: uppercase;
  letter-spacing: .06em; font-weight: 600; }
.stat .v { font-size: 24px; font-weight: 700; }
.stat .v.sm { font-size: 16px; font-weight: 600; margin-top: 4px; }
.stat .sub { font-size: 11.5px; color: var(--muted); }

/* toolbar */
.toolbar { display: flex; align-items: center; gap: 16px; margin-top: 28px;
  flex-wrap: wrap; }
.search { position: relative; flex: 1; min-width: 240px; max-width: 360px; }
.search svg { position: absolute; left: 10px; top: 50%; transform: translateY(-50%);
  color: var(--muted); }
.search input {
  width: 100%; height: 32px; padding: 0 10px 0 32px; font: inherit; font-size: 13px;
  border: 1px solid var(--border); border-radius: 6px; background: #fff;
  color: var(--fg);
}
.segmented, .segctl { display: flex; border: 1px solid var(--border);
  border-radius: 6px; overflow: hidden; background: #fff; }
.segctl button {
  font: inherit; font-size: 12.5px; padding: 6px 12px; cursor: pointer;
  border: none; background: none; color: var(--muted);
  border-left: 1px solid var(--border);
}
.segctl button:first-child { border-left: none; }
.segctl button.active { background: var(--accent); color: #fff; font-weight: 600; }
.showing { margin-left: auto; font-size: 13px; color: var(--muted); }

/* groups */
.group-head { display: flex; align-items: center; gap: 12px; margin: 26px 0 8px;
  scroll-margin-top: 20px; }
.group-head .name { font-size: 13px; font-weight: 700; text-transform: uppercase;
  letter-spacing: .09em; color: #566370; }
.group-head .rule { height: 1px; flex: 1; background: var(--border); }
.group-head .pfs { display: flex; gap: 6px; font-size: 11.5px; }
.group-head.fail-head .name { color: var(--fail); }
.group-head.fail-head .rule { background: var(--fail); opacity: .25; }

/* test rows */
.test { border: 1px solid var(--border); border-radius: 11px; margin-bottom: 8px;
  overflow: hidden; background: #fff; }
.test .row {
  display: grid; grid-template-columns: 104px 1fr 232px 96px 20px; align-items: center;
  gap: 16px; padding: 13px 18px; cursor: pointer;
}
.test .row:hover { background: #f1f5f8; }
.badge { display: inline-flex; align-items: center; padding: 2px 10px;
  border-radius: 5px; font-size: 11.5px; font-weight: 600; color: #fff; }
.badge.passed { background: var(--pass); }
.badge.failed { background: var(--fail); }
.badge.skipped { background: var(--skip); }
.test .name { font-family: var(--mono); font-size: 14px; font-weight: 600;
  color: var(--fg); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.test .desc { font-size: 12px; color: var(--muted); white-space: nowrap;
  overflow: hidden; text-overflow: ellipsis; margin-top: 2px; }
.test .metric { font-family: var(--mono); font-size: 12.5px; }
.test .dur { font-family: var(--mono); font-size: 12.5px; color: var(--muted);
  text-align: right; }
.test .chev { color: #7a848d; font-size: 15px; transition: transform .18s ease; }
.test.open .chev { transform: rotate(90deg); }

/* expanded panel */
.panel { display: none; border-top: 1px solid var(--border); padding: 20px 18px 22px;
  background: var(--panel); }
.test.open .panel { display: block; animation: rowIn .2s ease; }
@keyframes rowIn { from { opacity: 0; transform: translateY(-4px); } to { opacity: 1; transform: none; } }
.compare { display: grid; grid-template-columns: 1fr 1fr; gap: 18px; }
.pane-head { display: flex; align-items: center; justify-content: space-between;
  margin-bottom: 10px; }
.pane-head .lbl { font-size: 11px; color: var(--muted2); text-transform: uppercase;
  letter-spacing: .07em; }
.chan { margin-left: 7px; padding: 1px 6px; border-radius: 4px;
  font-size: 9.5px; font-weight: 700; letter-spacing: .05em;
  background: var(--border); color: var(--muted2); }
.chan-color { background: rgba(72,122,144,.15); color: var(--accent); }
.chan-depth { background: rgba(111,119,129,.18); color: var(--dark); }
.stage { position: relative; border: 1px solid var(--border); border-radius: 9px;
  overflow: hidden; background: #000; cursor: pointer; aspect-ratio: 1 / 1; }
.stage .layer { position: absolute; inset: 0; width: 100%; height: 100%;
  object-fit: contain; display: none; }
.stage[data-ab=result] .layer[data-role=result],
.stage[data-ab=reference] .layer[data-role=groundTruth] { display: block; }
.tag { position: absolute; top: 9px; left: 9px; padding: 3px 9px; border-radius: 5px;
  font-size: 10.5px; font-weight: 600; letter-spacing: .03em; color: #fff; display: none; }
.stage[data-ab=result] .tag.actual { display: block; background: rgba(208,50,74,.85); }
.stage[data-ab=reference] .tag.reference { display: block; background: rgba(72,122,144,.85); }
.cap-line { font-size: 11px; color: var(--muted3); margin-top: 7px; text-align: center; }
.maskbox { position: relative; border: 1px solid var(--border); border-radius: 9px;
  overflow: hidden; background: #000; aspect-ratio: 1 / 1; }
.maskcanvas, .maskfallback { width: 100%; height: 100%; display: block;
  object-fit: contain; image-rendering: pixelated; }
.threshrow { display: flex; align-items: center; gap: 10px; margin-top: 8px;
  font-size: 11.5px; color: var(--muted); }
.threshrow .tlabel { text-transform: uppercase; letter-spacing: .05em; }
.threshrow input[type=range] { flex: 1; accent-color: var(--fail); }
.threshrow .tval { color: var(--dark); min-width: 2.6em; text-align: right; }
.overtoggle { display: flex; align-items: center; gap: 5px; font-size: 11.5px;
  color: var(--muted); cursor: pointer; }
.overtoggle input { accent-color: var(--accent); }
.abseg { font: inherit; font-size: 10.5px; padding: 2px 9px; cursor: pointer;
  border: 1px solid var(--border); background: #fff; color: var(--muted); }
.abseg:first-child { border-radius: 4px 0 0 4px; }
.abseg:last-child { border-radius: 0 4px 4px 0; border-left: none; }
.abseg.active { background: var(--accent); color: #fff; font-weight: 600;
  border-color: var(--accent); }
.skipnote { grid-column: 1 / -1; display: flex; align-items: center; gap: 12px;
  padding: 22px; border: 1px dashed #ccd3da; border-radius: 9px; background: #fff; }
.skipnote .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--skip);
  flex: none; }
.skipnote .h { font-size: 13px; font-weight: 600; color: var(--dark); }
.skipnote .r { font-size: 12.5px; color: var(--muted); margin-top: 3px; }

/* metrics + config tables */
.meta { display: grid; grid-template-columns: 1.3fr 1fr; gap: 22px; margin-top: 20px; }
.meta .lbl { font-size: 11px; color: var(--muted2); text-transform: uppercase;
  letter-spacing: .07em; margin-bottom: 9px; }
.tbl { border: 1px solid var(--border); border-radius: 9px; overflow: hidden; }
.tbl .thead, .tbl .trow { display: grid; grid-template-columns: 1fr 1fr 1fr 70px;
  padding: 9px 14px; align-items: center; }
.tbl .thead { background: var(--thead); font-size: 10.5px; color: var(--muted2);
  text-transform: uppercase; letter-spacing: .05em; }
.tbl .trow { border-top: 1px solid var(--border-soft); font-size: 12.5px; }
.tbl .trow .res { text-align: right; font-weight: 600; }
.cfg { display: flex; flex-direction: column; border: 1px solid var(--border);
  border-radius: 9px; overflow: hidden; }
.cfg .r { display: flex; justify-content: space-between; gap: 16px; padding: 9px 14px;
  font-size: 12.5px; }
.cfg .r + .r { border-top: 1px solid var(--border-soft); }
.cfg .r .kk { color: var(--muted); flex: none; }
.cfg .r .vv { font-family: var(--mono); color: var(--dark); overflow: hidden;
  text-overflow: ellipsis; white-space: nowrap; }
.pass-fg { color: var(--pass); } .fail-fg { color: var(--fail); }
.skip-fg { color: var(--muted2); } .dark-fg { color: var(--dark); }
.no-images { color: var(--muted3); font-size: 12px; font-style: italic;
  padding: 20px; text-align: center; border: 1px dashed #ccd3da; border-radius: 9px;
  background: #fff; grid-column: 1 / -1; }
.empty { text-align: center; padding: 70px 20px; color: var(--muted3); }
.empty .h { font-size: 15px; font-weight: 600; color: #9aa4ad; }
.empty .s { font-size: 13px; margin-top: 6px; }
)CSS";

// Client behavior: status + text filtering, row expand/collapse, A/B reference
// vs. actual flip, and diff/threshold mask toggle. No dependencies.
constexpr const char *kScript = R"JS(
const list = document.getElementById('list');
const failHead = document.getElementById('failed-head');
const rows = Array.from(list.querySelectorAll('.test'));
const original = Array.from(list.children);
const origIndex = new Map(original.map((el, i) => [el, i]));
const search = document.getElementById('search');
const statusSegs = Array.from(document.querySelectorAll('.statusseg'));
const sortSegs = Array.from(document.querySelectorAll('.sortseg'));
let status = 'all';
const activeSort = document.querySelector('.sortseg.active');
let sort = activeSort ? activeSort.dataset.sort : 'category';

// Arrange the DOM for the active sort. "failed first" lifts failed rows into a
// prepended "Failed" section, then keeps the remaining rows under their own
// category headers; "category" restores the natural grouped order (the Failed
// section header then has no rows before the first category and hides itself).
function layout() {
  if (sort === 'failed') {
    const failed = rows.filter(r => r.dataset.verdict === 'failed')
      .sort((a, b) => origIndex.get(a) - origIndex.get(b));
    const rest = original.filter(el => el !== failHead
      && !(el.classList.contains('test') && el.dataset.verdict === 'failed'));
    list.appendChild(failHead);
    failed.forEach(r => list.appendChild(r));
    rest.forEach(el => list.appendChild(el));
  } else {
    original.forEach(el => list.appendChild(el));
  }
}

function applyFilter() {
  layout();

  const q = search.value.trim().toLowerCase();
  let shown = 0;
  for (const r of rows) {
    const ok = (status === 'all' || r.dataset.verdict === status)
      && (!q || r.dataset.search.includes(q));
    r.hidden = !ok;
    if (ok) shown++;
  }

  // Each header's P/F/S counts reflect only the rows currently shown under it,
  // and the header hides when it has none — in whatever order is current.
  let head = null, p = 0, f = 0, s = 0;
  const flush = () => {
    if (!head) return;
    const set = (k, v, suffix) => {
      const e = head.querySelector('[data-c=' + k + ']');
      if (e) e.textContent = v + suffix;
    };
    set('p', p, 'P'); set('f', f, 'F'); set('s', s, 'S');
    head.hidden = p + f + s === 0;
  };
  for (const el of Array.from(list.children)) {
    if (el.classList.contains('group-head')) { flush(); head = el; p = f = s = 0; }
    else if (el.classList.contains('test') && !el.hidden) {
      const v = el.dataset.verdict;
      if (v === 'passed') p++; else if (v === 'failed') f++; else s++;
    }
  }
  flush();

  document.getElementById('shown').textContent = shown;
  document.getElementById('noresults').hidden = shown > 0;
}
statusSegs.forEach(b => b.addEventListener('click', () => {
  status = b.dataset.status;
  statusSegs.forEach(x => x.classList.toggle('active', x === b));
  applyFilter();
}));
sortSegs.forEach(b => b.addEventListener('click', () => {
  sort = b.dataset.sort;
  sortSegs.forEach(x => x.classList.toggle('active', x === b));
  applyFilter();
}));
search.addEventListener('input', applyFilter);

// Expand / collapse a test row; initialize its difference-mask canvases the
// first time it opens (lazy — most rows are never expanded).
document.querySelectorAll('.test .row').forEach(row => {
  row.addEventListener('click', () => {
    const test = row.closest('.test');
    if (test.classList.toggle('open'))
      test.querySelectorAll('.mask-pane').forEach(initMask);
  });
});

// A/B flip between reference (ground truth) and actual (this run).
function setAB(stage, v) {
  stage.dataset.ab = v;
  const compare = stage.closest('.compare-pane');
  compare.querySelectorAll('.abseg').forEach(
    b => b.classList.toggle('active', b.dataset.ab === v));
}
document.querySelectorAll('.stage').forEach(stage => {
  stage.addEventListener('click',
    () => setAB(stage, stage.dataset.ab === 'result' ? 'reference' : 'result'));
});
document.querySelectorAll('.abseg').forEach(b => b.addEventListener('click', () => {
  setAB(b.closest('.compare-pane').querySelector('.stage'), b.dataset.ab);
}));

// Interactive difference-mask: threshold the diff image live and paint the
// exceeded pixels red, optionally over a dimmed actual render.
function renderMask(p) {
  const ctx = p._ctx, w = p._w, h = p._h;
  ctx.clearRect(0, 0, w, h);
  if (p._over.checked && p._actual) {
    ctx.drawImage(p._actual, 0, 0, w, h);
    ctx.fillStyle = 'rgba(255,255,255,0.65)';
    ctx.fillRect(0, 0, w, h);
  } else {
    ctx.fillStyle = '#111';
    ctx.fillRect(0, 0, w, h);
  }
  const t = +p._slider.value, src = p._diff.data, m = p._mask.data;
  for (let i = 0; i < src.length; i += 4) {
    if (Math.max(src[i], src[i + 1], src[i + 2]) > t) {
      m[i] = 255; m[i + 1] = 0; m[i + 2] = 0; m[i + 3] = 255;
    } else {
      m[i + 3] = 0;
    }
  }
  p._maskCtx.putImageData(p._mask, 0, 0);
  ctx.drawImage(p._maskCanvas, 0, 0, w, h);
}
function initMask(pane) {
  if (pane._init) return;
  pane._init = true;
  const canvas = pane.querySelector('.maskcanvas');
  if (!canvas) return;
  const slider = pane.querySelector('.threshslider');
  const tval = pane.querySelector('.tval');
  const over = pane.querySelector('.over-actual');
  const img = new Image();
  img.onload = () => {
    const w = img.naturalWidth, h = img.naturalHeight;
    const off = document.createElement('canvas');
    off.width = w; off.height = h;
    const octx = off.getContext('2d');
    octx.drawImage(img, 0, 0);
    let diff;
    try {
      diff = octx.getImageData(0, 0, w, h);
    } catch (e) {
      // Canvas tainted (referenced file:// diff) — fall back to the static image.
      const fb = document.createElement('img');
      fb.className = 'maskfallback';
      fb.src = canvas.dataset.diff;
      fb.alt = 'difference';
      canvas.replaceWith(fb);
      slider.disabled = true;
      over.disabled = true;
      slider.title = 'interactive thresholding needs an embedded (--embed) report';
      return;
    }
    canvas.width = w; canvas.height = h;
    pane._ctx = canvas.getContext('2d');
    pane._w = w; pane._h = h;
    pane._diff = diff;
    pane._maskCanvas = document.createElement('canvas');
    pane._maskCanvas.width = w; pane._maskCanvas.height = h;
    pane._maskCtx = pane._maskCanvas.getContext('2d');
    pane._mask = pane._maskCtx.createImageData(w, h);
    pane._slider = slider;
    pane._over = over;
    slider.addEventListener('input', () => { tval.textContent = slider.value; renderMask(pane); });
    over.addEventListener('change', () => renderMask(pane));
    // Reuse the sibling A/B "actual" image for the dimmed background.
    const actualEl = pane.closest('.compare').querySelector('.layer[data-role=result]');
    if (actualEl && actualEl.src) {
      const ai = new Image();
      ai.onload = () => { pane._actual = ai; if (over.checked) renderMask(pane); };
      ai.src = actualEl.src;
    }
    renderMask(pane);
  };
  img.src = canvas.dataset.diff;
}

applyFilter();
)JS";

// The ANARI wordmark (Khronos ANARI-Docs logo), inlined so the header stays
// self-contained. Fills use currentColor so the header can tint it with the
// accent (ANARI teal).
constexpr const char *kAnariLogo =
    R"SVG(<svg class="logo" viewBox="0 0 1500 500" role="img" aria-label="ANARI" xmlns="http://www.w3.org/2000/svg"><g><g><polygon fill="currentColor" points="1199.5,415.8 1191.4,415.8 1191.4,436.8 1184.4,436.8 1184.4,415.8 1176.3,415.8 1176.3,409.8 1199.5,409.8 "/><polygon fill="currentColor" points="1202.6,409.8 1213,409.8 1217.6,427.8 1217.7,427.8 1222.4,409.8 1232.7,409.8 1232.7,436.8 1226.1,436.8 1226.1,416.3 1226,416.3 1220.4,436.8 1215,436.8 1209.3,416.3 1209.2,416.3 1209.2,436.8 1202.6,436.8 "/></g><g><path fill="currentColor" d="M183.8,244.5h78.4l82.7,192.4h-68.7l-11.6-31.3h-84.1L169,436.8h-68.7L183.8,244.5z M222.9,292.9h-0.5 l-26.7,72.7h53.9L222.9,292.9z"/><path fill="currentColor" d="M354.1,244.5h73.8l89.2,120.4h0.5V244.5h62v192.4h-70.9l-92.1-121.5H416v121.5h-62V244.5H354.1z"/><path fill="currentColor" d="M672.2,244.5h78.4l82.7,192.4h-68.7L753,405.6h-84l-11.6,31.3h-68.7L672.2,244.5z M711.3,292.9h-0.5 l-26.7,72.7H738L711.3,292.9z"/><path fill="currentColor" d="M842.5,244.5h154.1c11.9,0,21.9,1.3,30,3.9c8.2,2.6,14.8,6.2,19.9,10.9c5.1,4.7,8.8,10.3,11,16.8 c2.2,6.6,3.4,13.9,3.4,22c0,12.9-3,22.9-8.9,30s-13.2,11.7-21.8,13.9v0.5c6.1,2,11.3,5.6,15.5,10.9s6.8,13.2,7.7,23.6 c0.7,9.5,1.4,17.5,2,24s1.3,12,2.2,16.6c0.8,4.6,1.8,8.4,3.1,11.3c1.3,3,2.9,5.6,4.9,7.9h-70.3c-1.8-4.1-2.9-8.8-3.2-14 c-0.4-5.2-0.5-10.1-0.5-14.5c0-7.5-0.6-13.6-1.8-18.3c-1.2-4.7-2.9-8.3-5.3-10.8c-2.3-2.5-5.1-4.2-8.4-5.1 c-3.2-0.9-6.8-1.3-10.8-1.3h-58.7v64.1h-64.1L842.5,244.5L842.5,244.5z M906.6,329.6h61.7c8.1,0,14.1-1.9,18.1-5.7 c3.9-3.8,5.9-9,5.9-15.6c0-6.3-2-11.3-5.9-15.1c-4-3.8-10-5.7-18.1-5.7h-61.7V329.6z"/><path fill="currentColor" d="M1090.1,244.5h64.1v192.4h-64.1V244.5z"/></g><path fill="currentColor" d="M1255,437.1c59.1-10.5,150.2-56,151.7-104.8c2.1-69.4-158.2-148.8-533.3-172.2 c-281.2-17.5-546.3,23.8-769.2,38.2C503.8,97.2,727.3,37.3,905.5,62c112.2,15.6,295.7,128.9,360.4,240.9 C1285.2,336.5,1291.4,384.7,1255,437.1z"/></g></svg>)SVG";

std::string toLower(std::string s)
{
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string categoryId(const std::string &category)
{
  std::string id = "cat-";
  for (char c : category)
    id += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
  return id;
}

std::string readFileBytes(const std::filesystem::path &path, bool &ok)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ok = false;
    return {};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  ok = true;
  return buffer.str();
}

// The src for a channel image, or empty when there is nothing to show: an
// absent path, a missing file, or an image excluded from an embedded file
// because its Case is not in the initial (embedded) selection. srcPrefix
// rebases the sidecar's workdir-relative path onto the HTML document's
// directory for non-embedded references.
std::string imageSrc(const std::filesystem::path &root,
    const std::string &relPath,
    bool embed,
    bool selected,
    const std::string &srcPrefix)
{
  if (relPath.empty())
    return {};
  const auto abs = root / relPath;
  if (embed) {
    if (!selected)
      return {};
    bool ok = false;
    const std::string bytes = readFileBytes(abs, ok);
    if (!ok)
      return {};
    return "data:image/png;base64," + base64Encode(bytes);
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(abs, ec))
    return {};
  return htmlEscape(srcPrefix + relPath);
}

// The prefix that rebases a workdir-relative image path onto the HTML
// document's directory, ending in '/' when non-empty. Falls back to the
// absolute workdir when no relative path exists (e.g. across drives).
std::string imageSrcPrefix(
    const std::filesystem::path &workdir, const std::filesystem::path &htmlPath)
{
  if (htmlPath.empty())
    return {};
  std::error_code ec;
  const auto htmlDir =
      std::filesystem::weakly_canonical(std::filesystem::absolute(htmlPath), ec)
          .parent_path();
  const auto absWorkdir =
      std::filesystem::weakly_canonical(std::filesystem::absolute(workdir), ec);
  auto base = std::filesystem::relative(absWorkdir, htmlDir, ec);
  if (ec || base.empty())
    base = absWorkdir;
  if (base == ".")
    return {};
  return base.generic_string() + "/";
}

// A finite score, formatted to a fixed precision, or an em dash for non-finite
// (e.g. PSNR of identical images, which the sidecar stores as null).
std::string fixed(double value, int precision)
{
  if (!std::isfinite(value))
    return "—";
  std::ostringstream os;
  os.setf(std::ios::fixed);
  os.precision(precision);
  os << value;
  return os.str();
}

// A metric passes strictly above its threshold; a non-finite (perfect) score
// always passes. With no threshold, defer to the channel verdict.
bool metricPasses(
    double value, const ChannelResult &ch, const std::string &name)
{
  auto thr = ch.thresholds.find(name);
  if (thr == ch.thresholds.end())
    return ch.passed;
  return !std::isfinite(value) || value > thr->second;
}

std::string axisText(const CaseResult &r)
{
  std::string out;
  for (const auto &[name, value] : r.axisValues) {
    if (!out.empty())
      out += ", ";
    out += name + "=" + value;
  }
  return out.empty() ? "—" : out;
}

const char *verdictClass(Verdict v)
{
  switch (v) {
  case Verdict::Passed:
    return "passed";
  case Verdict::Failed:
    return "failed";
  case Verdict::Skipped:
    return "skipped";
  }
  return "skipped";
}

// The channel name, upper-cased for the pane-head chip (e.g. "COLOR").
std::string channelTag(Channel c)
{
  std::string s = channelName(c);
  for (char &ch : s)
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

const char *verdictLabel(Verdict v)
{
  switch (v) {
  case Verdict::Passed:
    return "Passed";
  case Verdict::Failed:
    return "Failed";
  case Verdict::Skipped:
    return "Skipped";
  }
  return "Skipped";
}

// The one-line metric summary shown on a collapsed row (first channel).
std::string primaryMetric(const CaseResult &r)
{
  if (r.verdict == Verdict::Skipped || r.channels.empty())
    return "skipped";
  const ChannelResult &ch = r.channels.front();
  std::string out;
  auto ssim = ch.metrics.find("ssim");
  auto psnr = ch.metrics.find("psnr");
  if (ssim != ch.metrics.end())
    out += "SSIM " + fixed(ssim->second, 3);
  if (psnr != ch.metrics.end())
    out += (out.empty() ? "" : " · ") + std::string("PSNR ")
        + fixed(psnr->second, 1) + " dB";
  return out.empty() ? "—" : out;
}

void appendComparePane(std::ostringstream &os,
    const std::filesystem::path &root,
    const ChannelResult &ch,
    bool embed,
    bool selected,
    const std::string &srcPrefix)
{
  const std::string result =
      imageSrc(root, ch.resultImage, embed, selected, srcPrefix);
  const std::string truth =
      imageSrc(root, ch.groundTruthImage, embed, selected, srcPrefix);
  // Always embed the diff (the only image the mask canvas reads back), so its
  // interactive threshold and "over actual" work even in a non-embedded report
  // where a referenced file:// diff would taint the canvas. Bounded by the same
  // `selected` rule as embedding, so a failures-only report embeds only failed
  // cases' diffs. The larger result/reference images still follow `embed`.
  const std::string diff =
      imageSrc(root, ch.diffImage, /*embed=*/true, selected, srcPrefix);

  os << "<div class=\"compare\">";

  // A/B compare pane: click or the toggle flips reference vs. actual.
  os << "<div class=\"compare-pane\"><div class=\"pane-head\"><span "
        "class=\"lbl\">A / B compare<span class=\"chan chan-"
     << channelName(ch.channel) << "\">" << channelTag(ch.channel)
     << "</span></span>";
  if (!result.empty() && !truth.empty())
    os << "<span class=\"segmented\"><button type=\"button\" class=\"abseg "
          "active\" data-ab=\"result\">Actual</button><button type=\"button\" "
          "class=\"abseg\" data-ab=\"reference\">Reference</button></span>";
  os << "</div>";
  if (!result.empty() || !truth.empty()) {
    os << "<div class=\"stage\" data-ab=\"result\">";
    if (!result.empty())
      os << "<img class=\"layer\" data-role=\"result\" loading=\"lazy\" alt=\"actual\" "
            "src=\""
         << result << "\">";
    if (!truth.empty())
      os << "<img class=\"layer\" data-role=\"groundTruth\" loading=\"lazy\" "
            "alt=\"reference\" src=\""
         << truth << "\">";
    os << "<span class=\"tag actual\">ACTUAL — THIS RUN</span>"
          "<span class=\"tag reference\">REFERENCE — GROUND TRUTH</span></div>"
          "<div class=\"cap-line\">Click image or toggle to flip between "
          "reference and this run</div>";
  } else {
    os << "<div class=\"no-images\">images not embedded (regenerate with "
          "--embed --all)</div>";
  }
  os << "</div>"; // compare-pane

  // Difference-mask pane: an interactive canvas that thresholds the diff image
  // live (slider), paints exceeded pixels red, and can composite them over a
  // dimmed actual render. The canvas reads diff pixels; the diff is always
  // embedded (above) so this works regardless of --embed. The tainted-canvas
  // fallback below still guards cases whose diff is not embedded (an
  // initially-hidden passing case in a failures-only report).
  os << "<div class=\"mask-pane\"><div class=\"pane-head\"><span "
        "class=\"lbl\">Difference mask<span class=\"chan chan-"
     << channelName(ch.channel) << "\">" << channelTag(ch.channel)
     << "</span></span>";
  if (!diff.empty())
    os << "<label class=\"overtoggle\"><input type=\"checkbox\" "
          "class=\"over-actual\"> over actual</label>";
  os << "</div>";
  if (!diff.empty()) {
    os << "<div class=\"maskbox\"><canvas class=\"maskcanvas\" data-diff=\""
       << diff << "\"></canvas></div>";
    os << "<div class=\"threshrow\"><span class=\"tlabel\">threshold</span>"
          "<input type=\"range\" class=\"threshslider\" min=\"0\" max=\"255\" "
          "value=\"16\" aria-label=\"difference threshold\"><span class=\"tval "
          "kmono\">16</span></div>";
    os << "<div class=\"cap-line\">Pixels whose per-channel difference exceeds "
          "the threshold are painted red; “over actual” dims the render "
          "beneath them</div>";
  } else {
    os << "<div class=\"no-images\">no difference image</div>";
  }
  os << "</div>"; // mask-pane
  os << "</div>"; // compare
}

void appendMetricsTable(std::ostringstream &os, const CaseResult &r)
{
  os << "<div><div class=\"lbl\">Channel metrics</div><div class=\"tbl\">"
        "<div class=\"thead\"><div>Channel</div><div>SSIM</div><div>PSNR</div>"
        "<div class=\"res\">Result</div></div>";
  for (const auto &ch : r.channels) {
    auto ssim = ch.metrics.find("ssim");
    auto psnr = ch.metrics.find("psnr");
    const bool ok = ch.passed;
    os << "<div class=\"trow\"><div class=\"kmono dark-fg\">"
       << htmlEscape(channelName(ch.channel)) << "</div>";
    // SSIM
    os << "<div class=\"kmono "
       << (ssim != ch.metrics.end() && metricPasses(ssim->second, ch, "ssim")
                  ? "dark-fg"
                  : "fail-fg")
       << "\">";
    if (ssim != ch.metrics.end()) {
      os << fixed(ssim->second, 3);
      auto t = ch.thresholds.find("ssim");
      if (t != ch.thresholds.end())
        os << " <span class=\"skip-fg\">/ " << fixed(t->second, 2) << "</span>";
    } else
      os << "—";
    os << "</div>";
    // PSNR
    os << "<div class=\"kmono "
       << (psnr != ch.metrics.end() && metricPasses(psnr->second, ch, "psnr")
                  ? "dark-fg"
                  : "fail-fg")
       << "\">";
    if (psnr != ch.metrics.end()) {
      os << fixed(psnr->second, 1) << " dB";
      auto t = ch.thresholds.find("psnr");
      if (t != ch.thresholds.end())
        os << " <span class=\"skip-fg\">/ " << fixed(t->second, 0) << "</span>";
    } else
      os << "—";
    os << "</div>";
    os << "<div class=\"res " << (ok ? "pass-fg" : "fail-fg") << "\">"
       << (ok ? "PASS" : "FAIL") << "</div></div>";
  }
  os << "</div></div>";
}

void appendConfigTable(std::ostringstream &os, const CaseResult &r)
{
  const std::string device =
      r.device.library + " · " + r.device.device + " / " + r.device.renderer;
  const std::string dur = r.verdict == Verdict::Skipped
      ? std::string("—")
      : fixed(r.durationMs, 2) + " ms";
  os << "<div><div class=\"lbl\">Configuration</div><div class=\"cfg\">";
  auto rowKV =
      [&](const char *k, const std::string &v, const char *cls = "vv") {
        os << "<div class=\"r\"><span class=\"kk\">" << k
           << "</span><span class=\"" << cls << "\">"
           << htmlEscape(v.empty() ? "—" : v) << "</span></div>";
      };
  rowKV("Device", device);
  rowKV("Axes", axisText(r));
  rowKV("Detail", r.detail);
  rowKV("Duration", dur);
  os << "<div class=\"r\"><span class=\"kk\">Ground truth</span><span "
        "class=\"vv\" style=\"color:var(--link);\">"
     << htmlEscape(r.groundTruthKey.empty() ? "—" : r.groundTruthKey)
     << "</span></div>";
  os << "</div></div>";
}

bool hasAnyImage(const CaseResult &r)
{
  for (const auto &ch : r.channels)
    if (!ch.resultImage.empty() || !ch.groundTruthImage.empty()
        || !ch.diffImage.empty() || !ch.thresholdImage.empty())
      return true;
  return false;
}

void appendTestRow(std::ostringstream &os,
    const std::filesystem::path &root,
    const std::string &key,
    const CaseResult &r,
    bool embed,
    bool includeAll,
    const std::string &srcPrefix)
{
  // Embedded reports carry images only for the initially-shown Cases (failures
  // unless --all); passing Cases still appear, with their metadata.
  const bool selected = includeAll || r.verdict == Verdict::Failed;
  const char *vc = verdictClass(r.verdict);
  const std::string name = r.test + " / " + r.caseId;
  const std::string searchText =
      toLower(key + " " + r.description + " " + axisText(r));
  const char *metricFg = r.verdict == Verdict::Skipped
      ? "skip-fg"
      : (r.verdict == Verdict::Failed ? "fail-fg" : "pass-fg");
  const std::string dur = r.verdict == Verdict::Skipped
      ? std::string("—")
      : fixed(r.durationMs, 2) + " ms";

  os << "<div class=\"test\" data-verdict=\"" << vc << "\" data-category=\""
     << htmlEscape(r.category) << "\" data-key=\"" << htmlEscape(key)
     << "\" data-search=\"" << htmlEscape(searchText) << "\">";

  // Collapsed row.
  os << "<div class=\"row\"><div><span class=\"badge " << vc << "\">"
     << verdictLabel(r.verdict) << "</span></div>";
  os << "<div style=\"min-width:0;\"><div class=\"name\">" << htmlEscape(name)
     << "</div><div class=\"desc\">" << htmlEscape(r.description)
     << "</div></div>";
  os << "<div class=\"metric " << metricFg << "\">"
     << htmlEscape(primaryMetric(r)) << "</div>";
  os << "<div class=\"dur\">" << htmlEscape(dur) << "</div>";
  os << "<div class=\"chev\">›</div></div>";

  // Expanded panel.
  os << "<div class=\"panel\">";
  const bool skipped = r.verdict == Verdict::Skipped || r.channels.empty();
  if (skipped) {
    const std::string reason = !r.skipReason.empty() ? r.skipReason : r.detail;
    os << "<div class=\"compare\"><div class=\"skipnote\"><span "
          "class=\"dot\"></span><div><div class=\"h\">No images captured</div>"
          "<div class=\"r\">"
       << htmlEscape(reason.empty() ? "This case produced no rendered channels."
                                    : reason)
       << "</div></div></div></div>";
  } else if (!hasAnyImage(r)) {
    os << "<div class=\"compare\"><div class=\"no-images\">no images</div></div>";
  } else {
    // One comparison block per channel.
    for (const auto &ch : r.channels)
      appendComparePane(os, root, ch, embed, selected, srcPrefix);
  }
  if (!r.channels.empty()) {
    os << "<div class=\"meta\">";
    appendMetricsTable(os, r);
    appendConfigTable(os, r);
    os << "</div>";
  }
  os << "</div>"; // panel
  os << "</div>"; // test
}

std::string ringStyle(int passed, int failed, int total)
{
  const double denom = total > 0 ? total : 1;
  const double passDeg = passed / denom * 360.0;
  const double failEnd = (passed + failed) / denom * 360.0;
  std::ostringstream os;
  os.setf(std::ios::fixed);
  os.precision(1);
  os << "background:conic-gradient(var(--accent) 0 " << passDeg << "deg,"
     << "var(--fail) 0 " << failEnd << "deg,#d3d9de 0);";
  return os.str();
}

} // namespace

std::string htmlEscape(const std::string &text)
{
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&#39;";
      break;
    default:
      out += c;
    }
  }
  return out;
}

std::string base64Encode(const std::string &bytes)
{
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  size_t i = 0;
  const size_t n = bytes.size();
  while (i + 3 <= n) {
    const uint32_t v = (uint8_t(bytes[i]) << 16) | (uint8_t(bytes[i + 1]) << 8)
        | uint8_t(bytes[i + 2]);
    out += kTable[(v >> 18) & 0x3f];
    out += kTable[(v >> 12) & 0x3f];
    out += kTable[(v >> 6) & 0x3f];
    out += kTable[v & 0x3f];
    i += 3;
  }
  if (i + 1 == n) {
    const uint32_t v = uint8_t(bytes[i]) << 16;
    out += kTable[(v >> 18) & 0x3f];
    out += kTable[(v >> 12) & 0x3f];
    out += "==";
  } else if (i + 2 == n) {
    const uint32_t v = (uint8_t(bytes[i]) << 16) | (uint8_t(bytes[i + 1]) << 8);
    out += kTable[(v >> 18) & 0x3f];
    out += kTable[(v >> 12) & 0x3f];
    out += kTable[(v >> 6) & 0x3f];
    out += "=";
  }
  return out;
}

std::string generateHtml(const std::filesystem::path &workdir,
    const std::map<std::string, CaseResult> &results,
    const HtmlOptions &opts)
{
  const Summary s = summarize(results);
  const std::string srcPrefix =
      opts.embed ? std::string() : imageSrcPrefix(workdir, opts.htmlPath);
  const int executed = s.passed + s.failed;
  const int passRate = executed > 0
      ? static_cast<int>(std::lround(100.0 * s.passed / executed))
      : 0;
  double wallMs = 0.0;
  for (const auto &[key, r] : results)
    wallMs += r.durationMs;

  std::ostringstream warnings;
  const std::string label =
      deviceLabel(results, workdir.filename().string(), warnings);
  const std::string runName = workdir.filename().string();
  const std::string title = "ANARI CTS — " + runName;

  std::ostringstream os;
  os << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, "
        "initial-scale=1\"><title>"
     << htmlEscape(title) << "</title><style>" << kStyle
     << "</style></head><body><div class=\"wrap\">";

  // Header.
  os << "<header><div class=\"brand\">" << kAnariLogo
     << "<div><div class=\"title\">Conformance Test Suite</div>"
        "<div class=\"subtitle\">Image comparison — "
        "device under test: <span class=\"kmono\">"
     << htmlEscape(label) << "</span></div></div></div>";
  os << "<div class=\"runbox\">Run<br><span class=\"kmono\" "
        "style=\"font-size:13px;color:var(--dark);text-transform:none;"
        "letter-spacing:0;\">"
     << htmlEscape(runName) << "</span></div></header>";

  // Summary: pass-rate ring + stat cards.
  os << "<section class=\"summary\"><div class=\"ring-card\"><div class=\"ring\" "
        "style=\""
     << ringStyle(s.passed, s.failed, s.total) << "\"><div class=\"hole\">"
     << "<div class=\"pct\">" << passRate
     << "<span>%</span></div>"
        "<div class=\"cap\">pass rate</div></div></div><div class=\"legend\">"
        "<div class=\"row\"><span class=\"dot\" style=\"background:var(--pass);\">"
        "</span>Passed<span class=\"n kmono\">"
     << s.passed
     << "</span></div><div class=\"row\"><span class=\"dot\" "
        "style=\"background:var(--fail);\"></span>Failed<span class=\"n kmono\">"
     << s.failed
     << "</span></div><div class=\"row\"><span class=\"dot\" "
        "style=\"background:var(--skip);\"></span>Skipped<span class=\"n kmono\">"
     << s.skipped << "</span></div></div></div>";

  os << "<div class=\"stats\">";
  os << "<div class=\"stat\"><div class=\"k\">Total cases</div><div class=\"v "
        "kmono\">"
     << s.total << "</div><div class=\"sub\">" << executed
     << " executed</div></div>";
  os << "<div class=\"stat\"><div class=\"k\">Categories</div><div class=\"v "
        "kmono\">"
     << s.categories.size()
     << "</div><div class=\"sub\">domains covered</div></div>";
  os << "<div class=\"stat\"><div class=\"k\">Device under test</div><div "
        "class=\"v sm kmono\">"
     << htmlEscape(label) << "</div></div>";
  os << "<div class=\"stat\"><div class=\"k\">Wall time</div><div class=\"v "
        "kmono\">"
     << fixed(wallMs, 0)
     << " ms</div><div class=\"sub\">SSIM ≥ 0.70 · PSNR ≥ "
        "20</div></div>";
  os << "</div></section>";

  // Toolbar: search + status filter + count.
  os << "<section class=\"toolbar\"><div class=\"search\"><svg width=\"16\" "
        "height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\"><circle cx=\"11\" "
        "cy=\"11\" r=\"7\" stroke=\"currentColor\" stroke-width=\"2\"/><line "
        "x1=\"16.5\" y1=\"16.5\" x2=\"21\" y2=\"21\" stroke=\"currentColor\" "
        "stroke-width=\"2\" stroke-linecap=\"round\"/></svg><input "
        "type=\"search\" id=\"search\" placeholder=\"Search id / "
        "description…\"></div>";
  os << "<div class=\"segctl\">"
        "<button type=\"button\" class=\"statusseg active\" data-status=\"all\">All "
     << s.total
     << "</button><button type=\"button\" class=\"statusseg\" "
        "data-status=\"passed\">Passed "
     << s.passed
     << "</button><button type=\"button\" class=\"statusseg\" "
        "data-status=\"failed\">Failed "
     << s.failed
     << "</button><button type=\"button\" class=\"statusseg\" "
        "data-status=\"skipped\">Skipped "
     << s.skipped << "</button></div>";
  // Sort control. "Failed first" defaults on when the run has any failures, so
  // the most actionable Cases lead on open.
  const bool failedFirstDefault = s.failed > 0;
  os << "<div class=\"segctl\"><button type=\"button\" class=\"sortseg"
     << (failedFirstDefault ? " active" : "")
     << "\" data-sort=\"failed\">Failed first</button>"
        "<button type=\"button\" class=\"sortseg"
     << (failedFirstDefault ? "" : " active")
     << "\" data-sort=\"category\">By category</button></div>";
  os << "<div class=\"showing\">Showing <span class=\"kmono\" "
        "id=\"shown\">"
     << s.total << "</span> of <span class=\"kmono\">" << s.total
     << "</span></div></section>";

  // Category groups. Rows and their group headers are flat siblings in #list so
  // the client can either keep the grouping (headers shown) or re-sort rows
  // "failed first" across the whole list (headers hidden).
  os << "<section><div id=\"list\">";
  // Synthetic header for the "failed first" section; hidden until that sort is
  // active (and when the run has no failures). Labeled "All failed" when every
  // Case failed.
  const bool allFailed = s.failed > 0 && s.failed == s.total;
  os << "<div class=\"group-head fail-head\" id=\"failed-head\" hidden><span "
        "class=\"name\">"
     << (allFailed ? "All failed" : "Failed")
     << "</span><span class=\"rule\"></span><span class=\"pfs\"><span "
        "class=\"kmono fail-fg\" data-c=\"f\">"
     << s.failed << "F</span></span></div>";
  std::string currentCategory;
  for (const auto &[key, r] : results) {
    if (r.category != currentCategory) {
      currentCategory = r.category;
      const auto &c = s.categories.at(currentCategory);
      os << "<div class=\"group-head\" id=\"" << categoryId(currentCategory)
         << "\"><span class=\"name\">" << htmlEscape(currentCategory)
         << "</span><span class=\"rule\"></span><span class=\"pfs\">"
            "<span class=\"kmono pass-fg\" data-c=\"p\">"
         << c.passed << "P</span><span class=\"kmono fail-fg\" data-c=\"f\">"
         << c.failed << "F</span><span class=\"kmono skip-fg\" data-c=\"s\">"
         << c.skipped << "S</span></span></div>";
    }
    appendTestRow(os, workdir, key, r, opts.embed, opts.includeAll, srcPrefix);
  }
  os << "</div>"; // #list

  os << "<div class=\"empty\" id=\"noresults\" hidden><div class=\"h\">No cases "
        "match your filters</div><div class=\"s\">Try a different search term "
        "or status filter.</div></div>";
  os << "</section>";

  os << "</div><script>" << kScript << "</script></body></html>\n";
  return os.str();
}

} // namespace cts
} // namespace anari
