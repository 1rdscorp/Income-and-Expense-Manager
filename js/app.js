/* Ledgerly — Income & Expense Manager
 * Single-page app, vanilla JS, data stored locally in IndexedDB. */
(() => {
  'use strict';

  /* =========================================================
   * Utilities
   * ======================================================= */
  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));
  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const pad = (n) => String(n).padStart(2, '0');
  const uid = () => (crypto.randomUUID ? crypto.randomUUID() : 'id-' + Date.now().toString(36) + Math.random().toString(36).slice(2));

  const todayISO = () => { const d = new Date(); return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`; };
  const thisMonth = () => todayISO().slice(0, 7);
  const thisYear = () => Number(todayISO().slice(0, 4));
  const parseISO = (s) => { const [y, m, d] = s.split('-').map(Number); return new Date(y, (m || 1) - 1, d || 1); };
  const addMonths = (ym, n) => { const [y, m] = ym.split('-').map(Number); const d = new Date(y, m - 1 + n, 1); return `${d.getFullYear()}-${pad(d.getMonth() + 1)}`; };
  const monthLabel = (ym, style = 'long') => parseISO(ym + '-01').toLocaleDateString(undefined, { month: style, year: 'numeric' });
  const monthShort = (i) => new Date(2000, i, 1).toLocaleDateString(undefined, { month: 'short' });
  const monthLong = (i) => new Date(2000, i, 1).toLocaleDateString(undefined, { month: 'long' });
  const fmtDate = (s, opts = { day: 'numeric', month: 'short', year: 'numeric' }) => parseISO(s).toLocaleDateString(undefined, opts);
  const fmtDay = (s) => {
    if (s === todayISO()) return 'Today';
    const y = new Date(); y.setDate(y.getDate() - 1);
    if (s === `${y.getFullYear()}-${pad(y.getMonth() + 1)}-${pad(y.getDate())}`) return 'Yesterday';
    return parseISO(s).toLocaleDateString(undefined, { weekday: 'short', day: 'numeric', month: 'short', year: 'numeric' });
  };
  const fmtDateTime = (iso) => new Date(iso).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });
  const fmtBytes = (b) => b < 1024 ? `${b} B` : b < 1048576 ? `${(b / 1024).toFixed(0)} KB` : `${(b / 1048576).toFixed(1)} MB`;
  const pct = (a, b) => (b ? Math.round((a / b) * 1000) / 10 : 0);

  let moneyFmt, compactFmt, plainFmt;
  function buildFormatters() {
    const currency = state.settings.currency || 'USD';
    try {
      moneyFmt = new Intl.NumberFormat(undefined, { style: 'currency', currency });
      compactFmt = new Intl.NumberFormat(undefined, { style: 'currency', currency, notation: 'compact', maximumFractionDigits: 1 });
    } catch {
      moneyFmt = new Intl.NumberFormat(undefined, { style: 'currency', currency: 'USD' });
      compactFmt = new Intl.NumberFormat(undefined, { style: 'currency', currency: 'USD', notation: 'compact', maximumFractionDigits: 1 });
    }
    plainFmt = new Intl.NumberFormat(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2, useGrouping: false });
  }
  const money = (cents) => moneyFmt.format((cents || 0) / 100);
  const moneyCompact = (cents) => compactFmt.format((cents || 0) / 100);
  const signedMoney = (cents) => (cents < 0 ? '−' : '') + money(Math.abs(cents));
  const amountPlain = (cents) => plainFmt.format(cents / 100);

  /* Parse a user-entered amount (accepts "1,234.50", "1234,50", "$12") into integer cents. */
  function parseAmount(str) {
    let s = String(str || '').trim().replace(/[^\d.,-]/g, '');
    if (!s) return NaN;
    const lastDot = s.lastIndexOf('.'), lastComma = s.lastIndexOf(',');
    if (lastComma > lastDot) {
      // comma is decimal separator if followed by 1–2 digits, else thousands
      s = /,\d{1,2}$/.test(s) ? s.replace(/\./g, '').replace(',', '.') : s.replace(/,/g, '');
    } else s = s.replace(/,/g, '');
    const n = Number(s);
    return Number.isFinite(n) ? Math.round(n * 100) : NaN;
  }

  function download(filename, content, mime) {
    const blob = content instanceof Blob ? content : new Blob([content], { type: mime });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = filename;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 2000);
  }
  const csvCell = (v) => { const s = String(v ?? ''); return /[",\n\r]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s; };
  const toCSV = (rows) => '﻿' + rows.map((r) => r.map(csvCell).join(',')).join('\r\n');
  const slug = (s) => String(s).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'business';

  const blobToDataURL = (blob) => new Promise((res, rej) => { const r = new FileReader(); r.onload = () => res(r.result); r.onerror = () => rej(r.error); r.readAsDataURL(blob); });
  function dataURLToBlob(dataURL) {
    const m = /^data:([^;,]*)(;base64)?,(.*)$/s.exec(dataURL || '');
    if (!m) throw new Error('Invalid receipt data in backup');
    const mime = m[1] || 'application/octet-stream';
    if (!m[2]) return new Blob([decodeURIComponent(m[3])], { type: mime });
    const bin = atob(m[3]);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new Blob([bytes], { type: mime });
  }

  /* =========================================================
   * Icons
   * ======================================================= */
  const ICONS = {
    dashboard: '<rect x="3" y="3" width="7" height="9" rx="1.5"/><rect x="14" y="3" width="7" height="5" rx="1.5"/><rect x="14" y="12" width="7" height="9" rx="1.5"/><rect x="3" y="16" width="7" height="5" rx="1.5"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>',
    tag: '<path d="M20.6 13.4 13.4 20.6a2 2 0 0 1-2.8 0L3 13V3h10l7.6 7.6a2 2 0 0 1 0 2.8z"/><circle cx="7.5" cy="7.5" r="1.5"/>',
    chart: '<path d="M3 3v18h18"/><path d="M7 16v-4M12 16V8M17 16v-7"/>',
    database: '<ellipse cx="12" cy="5" rx="8" ry="3"/><path d="M4 5v6c0 1.7 3.6 3 8 3s8-1.3 8-3V5M4 11v6c0 1.7 3.6 3 8 3s8-1.3 8-3v-6"/>',
    settings: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    clip: '<path d="m21.4 11.1-9.2 9.2a6 6 0 0 1-8.5-8.5l9.2-9.2a4 4 0 0 1 5.7 5.7l-9.2 9.2a2 2 0 0 1-2.8-2.8l8.5-8.5"/>',
    trash: '<path d="M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6M10 11v6M14 11v6"/>',
    edit: '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/>',
    download: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"/>',
    upload: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M17 8l-5-5-5 5M12 3v12"/>',
    x: '<path d="M18 6 6 18M6 6l12 12"/>',
    menu: '<path d="M3 6h18M3 12h18M3 18h18"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="m21 21-4.3-4.3"/>',
    print: '<path d="M6 9V2h12v7M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2"/><rect x="6" y="14" width="12" height="8"/>',
    file: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/>',
    left: '<path d="m15 18-6-6 6-6"/>',
    right: '<path d="m9 18 6-6-6-6"/>',
    alert: '<path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4M12 17h.01"/>',
    check: '<path d="M20 6 9 17l-5-5"/>',
    camera: '<path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"/><circle cx="12" cy="13" r="4"/>',
    external: '<path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6M15 3h6v6M10 14 21 3"/>',
  };
  const icon = (name, cls = '') => `<svg class="i ${cls}" viewBox="0 0 24 24" aria-hidden="true">${ICONS[name] || ''}</svg>`;

  /* =========================================================
   * State
   * ======================================================= */
  const state = {
    settings: { ...DEFAULT_SETTINGS },
    categories: [],
    transactions: [],
    receiptCounts: new Map(),
    catMap: new Map(),
    txFilters: { mode: 'month', month: null, year: null, type: 'all', categoryId: 'all', q: '', limit: 150 },
    catTab: 'expense',
    report: { tab: 'monthly', month: null, year: null },
    lastCategory: { income: null, expense: null },
  };

  function indexCategories() {
    state.categories.sort((a, b) => a.name.localeCompare(b.name));
    state.catMap = new Map(state.categories.map((c) => [c.id, c]));
  }
  const catOf = (id) => state.catMap.get(id) || { id, name: 'Uncategorized', icon: '❔', type: 'expense' };
  const catsOfType = (type) => state.categories.filter((c) => c.type === type);

  const sortTx = () => state.transactions.sort((a, b) => (b.date.localeCompare(a.date)) || (b.createdAt || '').localeCompare(a.createdAt || ''));

  function totals(txs) {
    let income = 0, expense = 0;
    for (const t of txs) t.type === 'income' ? (income += t.amount) : (expense += t.amount);
    return { income, expense, net: income - expense, count: txs.length };
  }
  const txInMonth = (ym) => state.transactions.filter((t) => t.date.startsWith(ym));
  const txInYear = (y) => state.transactions.filter((t) => t.date.startsWith(String(y)));
  function byCategory(txs) {
    const m = new Map();
    for (const t of txs) {
      const e = m.get(t.categoryId) || { total: 0, count: 0 };
      e.total += t.amount; e.count++;
      m.set(t.categoryId, e);
    }
    return m;
  }
  function monthlySeries(year) {
    const out = Array.from({ length: 12 }, () => ({ income: 0, expense: 0 }));
    for (const t of txInYear(year)) {
      const i = Number(t.date.slice(5, 7)) - 1;
      out[i][t.type] += t.amount;
    }
    return out;
  }
  const yearsWithData = () => {
    const ys = new Set(state.transactions.map((t) => Number(t.date.slice(0, 4))));
    ys.add(thisYear());
    return [...ys].sort((a, b) => a - b);
  };

  async function saveSettings(patch) {
    Object.assign(state.settings, patch);
    await DB.setMeta('settings', state.settings);
    applySettings();
  }

  function applySettings() {
    const root = document.documentElement;
    if (state.settings.theme === 'light' || state.settings.theme === 'dark') root.dataset.theme = state.settings.theme;
    else delete root.dataset.theme;
    $('#brandName').textContent = state.settings.businessName || 'My Business';
    document.title = `${state.settings.businessName || 'Ledgerly'} — Income & Expense Manager`;
    buildFormatters();
    renderSidebarFoot();
  }

  function backupIsStale() {
    const days = Number(state.settings.backupReminderDays) || 0;
    if (!days || state.transactions.length === 0) return false;
    if (!state.settings.lastBackupAt) return true;
    return (Date.now() - new Date(state.settings.lastBackupAt).getTime()) / 86400000 >= days;
  }

  /* =========================================================
   * UI primitives: modal, confirm, toast
   * ======================================================= */
  const modalStack = [];
  let modalSeq = 0;
  function openModal({ title, body = '', footer = '', size = '', onClose } = {}) {
    const id = `m${++modalSeq}`;
    const wrap = document.createElement('div');
    wrap.className = 'modal-wrap';
    wrap.innerHTML = `
      <div class="modal ${size}" role="dialog" aria-modal="true" aria-labelledby="${id}-t">
        <header class="modal-head"><h2 id="${id}-t">${esc(title)}</h2>
          <button class="icon-btn" type="button" data-close aria-label="Close">${icon('x')}</button></header>
        <div class="modal-body"></div>
        <footer class="modal-foot"></footer>
      </div>`;
    const bodyEl = $('.modal-body', wrap);
    const footEl = $('.modal-foot', wrap);
    if (typeof body === 'string') bodyEl.innerHTML = body; else bodyEl.appendChild(body);
    if (typeof footer === 'string') footEl.innerHTML = footer; else footEl.appendChild(footer);
    const prevFocus = document.activeElement;
    let closed = false;
    const api = {
      el: wrap, body: bodyEl, foot: footEl,
      close() {
        if (closed) return;
        closed = true;
        wrap.remove();
        const i = modalStack.indexOf(api);
        if (i >= 0) modalStack.splice(i, 1);
        if (onClose) onClose();
        if (prevFocus && prevFocus.focus && document.contains(prevFocus)) prevFocus.focus();
      },
    };
    wrap.addEventListener('mousedown', (e) => { if (e.target === wrap) api.close(); });
    wrap.addEventListener('click', (e) => { if (e.target.closest('[data-close]')) api.close(); });
    $('#modalRoot').appendChild(wrap);
    modalStack.push(api);
    const focusable = $('input:not([type=hidden]):not([type=file]), select, textarea', bodyEl) || $('.modal-foot .btn.primary', wrap) || $('[data-close]', wrap);
    setTimeout(() => focusable && focusable.focus(), 30);
    return api;
  }

  function confirmDialog(message, { title = 'Are you sure?', okLabel = 'Confirm', danger = false } = {}) {
    return new Promise((resolve) => {
      let result = false;
      const m = openModal({
        title, size: 'sm',
        body: `<p>${message}</p>`,
        footer: `<button class="btn" type="button" data-close>Cancel</button>
                 <button class="btn ${danger ? 'danger solid' : 'primary'}" type="button" data-ok>${esc(okLabel)}</button>`,
        onClose: () => resolve(result),
      });
      $('[data-ok]', m.el).addEventListener('click', () => { result = true; m.close(); });
    });
  }

  function toast(msg, kind = 'ok') {
    const t = document.createElement('div');
    t.className = `toast ${kind === 'error' ? 'error' : ''}`;
    t.innerHTML = `${icon(kind === 'error' ? 'alert' : 'check')}<span>${esc(msg)}</span>`;
    $('#toastRoot').appendChild(t);
    setTimeout(() => t.remove(), kind === 'error' ? 5000 : 2600);
  }

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && modalStack.length) { modalStack[modalStack.length - 1].close(); return; }
    if (e.key === 'Escape' && $('#sidebar').classList.contains('open')) toggleSidebar(false);
    // "n" opens a new transaction (when not typing)
    if (e.key === 'n' && !modalStack.length && !e.metaKey && !e.ctrlKey && !e.altKey &&
        !/^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)) {
      e.preventDefault();
      openTransactionForm();
    }
  });

  /* =========================================================
   * Layout & routing
   * ======================================================= */
  const ROUTES = {
    dashboard: { title: 'Dashboard', icon: 'dashboard', render: renderDashboard },
    transactions: { title: 'Transactions', icon: 'list', render: renderTransactions },
    categories: { title: 'Categories', icon: 'tag', render: renderCategories },
    reports: { title: 'Reports', icon: 'chart', render: renderReports },
    backup: { title: 'Backup & Restore', icon: 'database', render: renderBackup, sep: true },
    settings: { title: 'Settings', icon: 'settings', render: renderSettings },
  };
  let cleanups = [];
  const onCleanup = (fn) => cleanups.push(fn);
  const currentRoute = () => {
    const r = (location.hash.replace(/^#\/?/, '').split('?')[0]) || 'dashboard';
    return ROUTES[r] ? r : 'dashboard';
  };
  const go = (route) => { if (currentRoute() === route) render(); else location.hash = `#/${route}`; };

  function renderNav() {
    $('#nav').innerHTML = Object.entries(ROUTES).map(([key, r]) =>
      `${r.sep ? '<div class="nav-sep" role="separator"></div>' : ''}<a href="#/${key}" data-route="${key}">${icon(r.icon)}<span>${esc(r.title)}</span></a>`).join('');
    $('#menuBtn').innerHTML = icon('menu');
    $('#quickAdd').innerHTML = `${icon('plus')}<span class="label">Add transaction</span>`;
    $('#quickAdd').title = 'Add transaction (N)';
  }

  function renderSidebarFoot() {
    const el = $('#sidebarFoot');
    if (!el) return;
    const last = state.settings.lastBackupAt;
    el.innerHTML = `<div><span class="backup-dot ${backupIsStale() ? 'stale' : ''}"></span>${last ? `Last backup ${esc(fmtDate(last.slice(0, 10)))}` : 'No backup yet'}</div>
      <div style="margin-top:4px">Data is stored on this device.</div>`;
  }

  function toggleSidebar(open) {
    const sb = $('#sidebar');
    const isOpen = open ?? !sb.classList.contains('open');
    sb.classList.toggle('open', isOpen);
    $('#scrim').hidden = !isOpen;
    $('#menuBtn').setAttribute('aria-expanded', String(isOpen));
  }

  function render() {
    cleanups.forEach((fn) => { try { fn(); } catch { /* ignore */ } });
    cleanups = [];
    const key = currentRoute();
    const route = ROUTES[key];
    $$('#nav a').forEach((a) => {
      const active = a.dataset.route === key;
      a.classList.toggle('active', active);
      if (active) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
    });
    $('#pageTitle').textContent = route.title;
    // Fresh container per render so per-view listeners never accumulate.
    const view = document.createElement('div');
    $('#view').replaceChildren(view);
    route.render(view);
    renderSidebarFoot();
  }

  /* =========================================================
   * Shared fragments
   * ======================================================= */
  function txRow(t, { showDate = false } = {}) {
    const c = catOf(t.categoryId);
    const rc = state.receiptCounts.get(t.id) || 0;
    const title = t.description || t.vendor || c.name;
    const subParts = [c.name];
    if (t.vendor && t.description) subParts.push(t.vendor);
    if (t.paymentMethod) subParts.push(t.paymentMethod);
    return `<button class="tx" type="button" data-tx="${esc(t.id)}">
      <span class="ico" aria-hidden="true">${esc(c.icon || '🏷️')}</span>
      <span style="min-width:0"><span class="main-line">${esc(title)}</span>
        <span class="sub-line">${showDate ? esc(fmtDate(t.date)) + ' · ' : ''}${esc(subParts.join(' · '))}</span></span>
      <span class="clip">${rc ? `${icon('clip')}<span>${rc}</span><span class="sr-only"> receipt${rc > 1 ? 's' : ''}</span>` : ''}</span>
      <span class="amount ${t.type}">${t.type === 'income' ? '+' : '−'}${esc(money(t.amount))}</span>
    </button>`;
  }
  function bindTxRows(root) {
    $$('[data-tx]', root).forEach((b) => b.addEventListener('click', () => {
      const t = state.transactions.find((x) => x.id === b.dataset.tx);
      if (t) openTransactionForm(t);
    }));
  }

  function deltaText(cur, prev, { upIsGood }) {
    if (!prev) return cur ? '<span class="muted">No data last month</span>' : '';
    const d = ((cur - prev) / Math.abs(prev)) * 100;
    if (!Number.isFinite(d)) return '';
    const up = d >= 0;
    const good = up === upIsGood;
    return `<span class="${Math.abs(d) < 0.5 ? '' : good ? 'good' : 'bad'}">${up ? '▲' : '▼'} ${Math.abs(d).toFixed(Math.abs(d) < 10 ? 1 : 0)}%</span> vs last month`;
  }

  function periodNav(label, id) {
    return `<div class="period-nav" id="${id}">
      <button class="icon-btn" type="button" data-step="-1" aria-label="Previous">${icon('left')}</button>
      <span class="lbl">${esc(label)}</span>
      <button class="icon-btn" type="button" data-step="1" aria-label="Next">${icon('right')}</button>
    </div>`;
  }

  function seg(id, options, value, cls = '') {
    return `<div class="seg ${cls}" id="${id}" role="group">${options.map(([v, l]) =>
      `<button type="button" data-value="${esc(v)}" class="${v === value ? 'active' : ''}" aria-pressed="${v === value}">${esc(l)}</button>`).join('')}</div>`;
  }
  function bindSeg(root, id, onChange) {
    const el = $(`#${id}`, root);
    el.addEventListener('click', (e) => {
      const b = e.target.closest('button[data-value]');
      if (!b) return;
      $$('button', el).forEach((x) => { x.classList.toggle('active', x === b); x.setAttribute('aria-pressed', String(x === b)); });
      onChange(b.dataset.value);
    });
  }

  function categoryOptions(type, selected, { includeAll = false } = {}) {
    const list = type === 'all' ? state.categories : catsOfType(type);
    const opt = (c) => `<option value="${esc(c.id)}" ${c.id === selected ? 'selected' : ''}>${esc(c.icon || '')} ${esc(c.name)}</option>`;
    let html = includeAll ? `<option value="all">All categories</option>` : '';
    if (type === 'all') {
      html += `<optgroup label="Income">${catsOfType('income').map(opt).join('')}</optgroup>`;
      html += `<optgroup label="Expenses">${catsOfType('expense').map(opt).join('')}</optgroup>`;
    } else html += list.map(opt).join('');
    return html;
  }

  /* =========================================================
   * Dashboard
   * ======================================================= */
  function renderDashboard(view) {
    const ym = thisMonth();
    const y = thisYear();
    const today = todayISO();
    const m = totals(txInMonth(ym));
    const p = totals(txInMonth(addMonths(ym, -1)));
    const yt = totals(txInYear(y));
    const td = totals(state.transactions.filter((t) => t.date === today));
    const monthName = monthLabel(ym, 'long');

    let html = '';
    if (backupIsStale()) {
      html += `<div class="banner" style="margin-bottom:16px">${icon('alert')}
        <p><b>Time for a backup.</b> ${state.settings.lastBackupAt ? `Your last backup was on ${esc(fmtDate(state.settings.lastBackupAt.slice(0, 10)))}.` : 'You haven’t backed up your data yet.'} Data lives only in this browser — keep a copy somewhere safe.</p>
        <button class="btn sm" type="button" data-act="backup-now">${icon('download')} Back up now</button></div>`;
    }

    if (!state.transactions.length) {
      html += `<div class="card"><div class="empty">
        <div class="big" aria-hidden="true">📒</div>
        <h3>Welcome to ${esc(state.settings.businessName || 'your ledger')}</h3>
        <p>Record your daily income and expenses, attach receipts, and see monthly and yearly totals. ${state.categories.length} standard business categories are ready to use.</p>
        <div class="row" style="justify-content:center">
          <button class="btn primary" type="button" data-act="add">${icon('plus')} Add your first transaction</button>
          <button class="btn" type="button" data-act="sample">Load sample data</button>
        </div></div></div>`;
      view.innerHTML = html;
      bindDashboardActions(view);
      return;
    }

    html += `<div class="kpis">
      <div class="card kpi"><div class="label">Spent today</div>
        <div class="value">${esc(money(td.expense))}</div>
        <div class="meta">${td.count ? `${td.count} transaction${td.count > 1 ? 's' : ''} today${td.income ? ` · ${esc(money(td.income))} received` : ''}` : 'Nothing recorded yet today'}</div></div>
      <div class="card kpi"><div class="label"><span class="swatch" style="background:var(--series-income)"></span>Income · ${esc(monthLabel(ym, 'short'))}</div>
        <div class="value">${esc(money(m.income))}</div><div class="meta">${deltaText(m.income, p.income, { upIsGood: true })}</div></div>
      <div class="card kpi"><div class="label"><span class="swatch" style="background:var(--series-expense)"></span>Expenses · ${esc(monthLabel(ym, 'short'))}</div>
        <div class="value">${esc(money(m.expense))}</div><div class="meta">${deltaText(m.expense, p.expense, { upIsGood: false })}</div></div>
      <div class="card kpi"><div class="label">Net profit · ${esc(monthLabel(ym, 'short'))}</div>
        <div class="value ${m.net < 0 ? 'bad' : ''}">${esc(signedMoney(m.net))}</div>
        <div class="meta">${m.income ? `${pct(m.net, m.income)}% margin` : 'No income yet this month'}</div></div>
      <div class="card kpi"><div class="label">Net profit · ${y} to date</div>
        <div class="value ${yt.net < 0 ? 'bad' : ''}">${esc(signedMoney(yt.net))}</div>
        <div class="meta">${esc(moneyCompact(yt.income))} in · ${esc(moneyCompact(yt.expense))} out</div></div>
    </div>`;

    html += `<div class="grid dash" style="margin-top:16px">
      <div class="card">
        <div class="card-head"><h2>Income vs expenses <span class="sub">· ${y}</span></h2>
          <div class="legend"><span><i class="swatch" style="background:var(--series-income)"></i>Income</span><span><i class="swatch" style="background:var(--series-expense)"></i>Expenses</span></div></div>
        <div id="dashChart"></div>
        <p class="small muted" style="margin:8px 0 0">Click a month to see its transactions.</p>
      </div>
      <div class="card">
        <div class="card-head"><h2>Top spending <span class="sub">· ${esc(monthName)}</span></h2>
          <a class="small" href="#/reports">Full report</a></div>
        <div id="topCats"></div>
      </div>
    </div>`;

    html += `<div class="grid cols-2" style="margin-top:16px">
      <div class="card"><div class="card-head"><h2>Recent transactions</h2><a class="small" href="#/transactions">View all</a></div>
        <div class="tx-list tx-compact" id="recent"></div></div>
      <div class="card"><div class="card-head"><h2>Last 7 days</h2><span class="sub">Daily totals</span></div>
        <div id="week"></div></div>
    </div>`;

    view.innerHTML = html;

    // Chart
    const series = monthlySeries(y);
    onCleanup(Charts.groupedBars($('#dashChart', view), {
      labels: Array.from({ length: 12 }, (_, i) => monthShort(i)),
      fullLabels: Array.from({ length: 12 }, (_, i) => `${monthLong(i)} ${y}`),
      series: [
        { name: 'Income', colorVar: '--series-income', values: series.map((s) => s.income) },
        { name: 'Expenses', colorVar: '--series-expense', values: series.map((s) => s.expense) },
      ],
      format: money, compact: moneyCompact,
      ariaLabel: `Monthly income and expenses for ${y}`,
      footer: (i) => { const n = series[i].income - series[i].expense; return `<div class="tip-foot"><span>Net</span><b class="${n < 0 ? 'bad' : ''}">${esc(signedMoney(n))}</b></div>`; },
      onSelect: (i) => {
        Object.assign(state.txFilters, { mode: 'month', month: `${y}-${pad(i + 1)}`, type: 'all', categoryId: 'all', q: '' });
        go('transactions');
      },
    }));

    // Top categories (expenses this month)
    const bc = byCategory(txInMonth(ym).filter((t) => t.type === 'expense'));
    const rows = [...bc.entries()].map(([id, v]) => ({ c: catOf(id), ...v })).sort((a, b) => b.total - a.total);
    const top = rows.slice(0, 6);
    const rest = rows.slice(6).reduce((s, r) => s + r.total, 0);
    const maxV = Math.max(1, ...top.map((r) => Math.max(r.total, r.c.budget || 0)), rest);
    const budgeted = catsOfType('expense').filter((c) => c.budget > 0 && !bc.has(c.id)).slice(0, Math.max(0, 6 - top.length));
    $('#topCats', view).innerHTML = top.length || budgeted.length ? `<div class="hbars">
      ${top.map((r) => hbar(r.c, r.total, r.count, maxV, m.expense)).join('')}
      ${rest ? hbar({ name: `Other (${rows.length - 6} categories)`, icon: '…' }, rest, null, maxV, m.expense) : ''}
      ${budgeted.map((c) => hbar(c, 0, 0, Math.max(maxV, c.budget), m.expense)).join('')}
    </div>` : `<div class="empty"><p>No expenses recorded in ${esc(monthName)} yet.</p></div>`;

    // Recent
    $('#recent', view).innerHTML = state.transactions.slice(0, 7).map((t) => txRow(t, { showDate: true })).join('');
    bindTxRows($('#recent', view));

    // Last 7 days
    const days = [];
    for (let i = 0; i < 7; i++) {
      const d = new Date(); d.setDate(d.getDate() - i);
      const iso = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
      days.push({ iso, ...totals(state.transactions.filter((t) => t.date === iso)) });
    }
    const weekMax = Math.max(1, ...days.map((d) => d.expense));
    $('#week', view).innerHTML = `<div class="hbars">${days.map((d) => `
      <div class="hbar"><div class="top"><span class="name">${esc(fmtDay(d.iso))}</span>
        ${d.income ? `<span class="small good num">+${esc(money(d.income))}</span>` : ''}
        <span class="amt">${esc(money(d.expense))}</span></div>
        <div class="track"><div class="fill" style="width:${d.expense ? Math.max(1, (d.expense / weekMax) * 100) : 0}%"></div></div></div>`).join('')}</div>
      <p class="small muted" style="margin:10px 0 0">Bars show expenses; income shown in green.</p>`;

    bindDashboardActions(view);
  }

  function hbar(c, total, count, max, monthTotal) {
    const budget = c.budget || 0;
    const over = budget && total > budget;
    const sub = [];
    if (count != null) sub.push(`${count} transaction${count === 1 ? '' : 's'}`);
    if (monthTotal) sub.push(`${pct(total, monthTotal)}% of spending`);
    let budgetTxt = '';
    if (budget) budgetTxt = over
      ? `<span class="bad">${icon('alert', 'small')} Over budget by ${esc(money(total - budget))}</span>`
      : `<span>${esc(money(budget - total))} left of ${esc(money(budget))} budget</span>`;
    return `<div class="hbar"><div class="top"><span aria-hidden="true">${esc(c.icon || '')}</span><span class="name">${esc(c.name)}</span><span class="amt">${esc(money(total))}</span></div>
      <div class="track"><div class="fill ${over ? 'over' : ''}" style="width:${total ? Math.max(1, (total / max) * 100) : 0}%"></div></div>
      <div class="sub">${sub.length ? `<span>${sub.join(' · ')}</span>` : ''}${budgetTxt ? `${sub.length ? '<span>·</span>' : ''}${budgetTxt}` : ''}</div></div>`;
  }

  function bindDashboardActions(view) {
    view.addEventListener('click', async (e) => {
      const b = e.target.closest('[data-act]');
      if (!b) return;
      if (b.dataset.act === 'add') openTransactionForm();
      if (b.dataset.act === 'backup-now') await createBackup(true);
      if (b.dataset.act === 'sample') await loadSampleData();
    });
  }

  /* =========================================================
   * Transactions
   * ======================================================= */
  function filteredTransactions() {
    const f = state.txFilters;
    const q = f.q.trim().toLowerCase();
    return state.transactions.filter((t) => {
      if (f.mode === 'month' && !t.date.startsWith(f.month)) return false;
      if (f.mode === 'year' && !t.date.startsWith(String(f.year))) return false;
      if (f.type !== 'all' && t.type !== f.type) return false;
      if (f.categoryId !== 'all' && t.categoryId !== f.categoryId) return false;
      if (q) {
        const hay = `${t.description} ${t.vendor} ${t.notes} ${t.paymentMethod} ${catOf(t.categoryId).name} ${amountPlain(t.amount)}`.toLowerCase();
        if (!hay.includes(q)) return false;
      }
      return true;
    });
  }

  function renderTransactions(view) {
    const f = state.txFilters;
    f.month = f.month || thisMonth();
    f.year = f.year || thisYear();
    const label = f.mode === 'month' ? monthLabel(f.month) : f.mode === 'year' ? String(f.year) : 'All time';
    view.innerHTML = `
      <div class="toolbar">
        ${seg('txMode', [['month', 'Month'], ['year', 'Year'], ['all', 'All']], f.mode)}
        ${f.mode !== 'all' ? periodNav(label, 'txPeriod') : ''}
        <select class="input" id="txType" aria-label="Type">
          <option value="all">Income &amp; expenses</option>
          <option value="income" ${f.type === 'income' ? 'selected' : ''}>Income only</option>
          <option value="expense" ${f.type === 'expense' ? 'selected' : ''}>Expenses only</option>
        </select>
        <select class="input" id="txCat" aria-label="Category">${categoryOptions(f.type === 'all' ? 'all' : f.type, f.categoryId, { includeAll: true })}</select>
        <label class="search"><span class="sr-only">Search</span>${icon('search')}<input class="input" id="txSearch" type="search" placeholder="Search description, vendor, notes…" value="${esc(f.q)}"></label>
        <button class="btn" type="button" id="txExport" title="Export the filtered list as CSV">${icon('download')} CSV</button>
      </div>
      <div class="summary-strip" id="txSummary"></div>
      <div class="card" id="txListCard"></div>`;

    bindSeg(view, 'txMode', (v) => { f.mode = v; f.limit = 150; render(); });
    const pn = $('#txPeriod', view);
    if (pn) pn.addEventListener('click', (e) => {
      const b = e.target.closest('[data-step]');
      if (!b) return;
      const step = Number(b.dataset.step);
      if (f.mode === 'month') f.month = addMonths(f.month, step); else f.year += step;
      f.limit = 150;
      render();
    });
    $('#txType', view).addEventListener('change', (e) => {
      f.type = e.target.value;
      if (f.categoryId !== 'all' && f.type !== 'all' && catOf(f.categoryId).type !== f.type) f.categoryId = 'all';
      render();
    });
    $('#txCat', view).addEventListener('change', (e) => { f.categoryId = e.target.value; f.limit = 150; updateTxList(view); });
    let tmr;
    $('#txSearch', view).addEventListener('input', (e) => {
      clearTimeout(tmr);
      tmr = setTimeout(() => { f.q = e.target.value; f.limit = 150; updateTxList(view); }, 150);
    });
    $('#txExport', view).addEventListener('click', () => {
      const list = filteredTransactions();
      if (!list.length) return toast('Nothing to export', 'error');
      download(`transactions-${slug(label)}.csv`, transactionsCSV(list), 'text/csv');
    });
    updateTxList(view);
  }

  function updateTxList(view) {
    const f = state.txFilters;
    const list = filteredTransactions();
    const t = totals(list);
    $('#txSummary', view).innerHTML = `
      <div class="card"><div class="lbl">Income</div><div class="val good">${esc(money(t.income))}</div></div>
      <div class="card"><div class="lbl">Expenses</div><div class="val">${esc(money(t.expense))}</div></div>
      <div class="card"><div class="lbl">Net</div><div class="val ${t.net < 0 ? 'bad' : ''}">${esc(signedMoney(t.net))}</div></div>
      <div class="card"><div class="lbl">Transactions</div><div class="val">${t.count}</div></div>`;

    const card = $('#txListCard', view);
    if (!list.length) {
      card.innerHTML = `<div class="empty"><div class="big" aria-hidden="true">🧾</div><h3>No transactions found</h3>
        <p>${state.transactions.length ? 'Try a different period or clear the filters.' : 'Add your first income or expense to get started.'}</p>
        <button class="btn primary" type="button" id="emptyAdd">${icon('plus')} Add transaction</button></div>`;
      $('#emptyAdd', card).addEventListener('click', () => openTransactionForm());
      return;
    }
    const shown = list.slice(0, f.limit);
    const groups = new Map();
    for (const tx of shown) {
      if (!groups.has(tx.date)) groups.set(tx.date, []);
      groups.get(tx.date).push(tx);
    }
    let html = '<div class="tx-list">';
    for (const [date, txs] of groups) {
      const dt = totals(list.filter((x) => x.date === date));
      html += `<div class="day-group"><div class="day-head"><span>${esc(fmtDay(date))}</span>
        <span class="tot">${dt.income ? `<span class="good">+${esc(money(dt.income))}</span> · ` : ''}${dt.expense ? `Spent ${esc(money(dt.expense))}` : ''}</span></div>
        ${txs.map((x) => txRow(x)).join('')}</div>`;
    }
    html += '</div>';
    if (list.length > shown.length) html += `<div class="row" style="justify-content:center;margin-top:16px"><button class="btn" type="button" id="more">Show more (${list.length - shown.length} remaining)</button></div>`;
    card.innerHTML = html;
    bindTxRows(card);
    const more = $('#more', card);
    if (more) more.addEventListener('click', () => { f.limit += 300; updateTxList(view); });
  }

  function transactionsCSV(list) {
    const rows = [['Date', 'Type', 'Category', 'Description', 'Vendor / Payee', 'Payment method', 'Amount', 'Signed amount', 'Receipts', 'Notes']];
    for (const t of [...list].sort((a, b) => a.date.localeCompare(b.date))) {
      rows.push([t.date, t.type === 'income' ? 'Income' : 'Expense', catOf(t.categoryId).name, t.description, t.vendor, t.paymentMethod,
        amountPlain(t.amount), amountPlain(t.type === 'income' ? t.amount : -t.amount), state.receiptCounts.get(t.id) || 0, t.notes]);
    }
    return toCSV(rows);
  }

  /* =========================================================
   * Transaction form (add / edit) with receipts
   * ======================================================= */
  async function compressImage(file) {
    if (!state.settings.compressReceipts) return file;
    if (!/^image\/(jpeg|png|webp|bmp)$/.test(file.type) || file.size < 700 * 1024) return file;
    try {
      const bmp = await createImageBitmap(file);
      const max = 2000;
      const scale = Math.min(1, max / Math.max(bmp.width, bmp.height));
      const canvas = document.createElement('canvas');
      canvas.width = Math.round(bmp.width * scale);
      canvas.height = Math.round(bmp.height * scale);
      const ctx = canvas.getContext('2d');
      ctx.fillStyle = '#fff';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.drawImage(bmp, 0, 0, canvas.width, canvas.height);
      if (bmp.close) bmp.close();
      const blob = await new Promise((res) => canvas.toBlob(res, 'image/jpeg', 0.82));
      if (!blob || blob.size >= file.size) return file;
      return new File([blob], file.name.replace(/\.[^.]+$/, '') + '.jpg', { type: 'image/jpeg' });
    } catch {
      return file;
    }
  }

  async function openTransactionForm(existing = null, presetType = null) {
    const isEdit = !!existing;
    const t = existing ? { ...existing } : {
      type: presetType || 'expense', date: todayISO(), amount: 0, categoryId: null,
      description: '', vendor: '', paymentMethod: '', notes: '',
    };
    const urls = [];
    const objURL = (blob) => { const u = URL.createObjectURL(blob); urls.push(u); return u; };

    // receipts: existing (from DB) + new (pending)
    let receipts = isEdit ? (await DB.receiptsFor(t.id)).map((r) => ({ ...r, isNew: false })) : [];
    const removed = [];

    const body = document.createElement('form');
    body.noValidate = true;
    body.innerHTML = `
      <div class="form-grid">
        <div class="full">${seg('fType', [['expense', 'Expense'], ['income', 'Income']], t.type, 'type-toggle')}</div>
        <label class="field"><span>Amount</span>
          <input class="input amount" name="amount" inputmode="decimal" autocomplete="off" placeholder="0.00" value="${t.amount ? esc(amountPlain(t.amount)) : ''}" required></label>
        <label class="field"><span>Date</span>
          <input class="input" type="date" name="date" value="${esc(t.date)}" required></label>
        <label class="field full"><span>Category</span>
          <select class="input" name="categoryId" required></select></label>
        <label class="field full"><span>Description</span>
          <input class="input" name="description" maxlength="200" placeholder="e.g. Printer paper, Invoice #1042" value="${esc(t.description)}"></label>
        <label class="field"><span id="vendorLbl">${t.type === 'income' ? 'Customer / payer' : 'Vendor / payee'}</span>
          <input class="input" name="vendor" maxlength="120" list="vendorList" value="${esc(t.vendor)}"></label>
        <label class="field"><span>Payment method</span>
          <select class="input" name="paymentMethod"><option value="">—</option>${PAYMENT_METHODS.map((p) => `<option ${p === t.paymentMethod ? 'selected' : ''}>${esc(p)}</option>`).join('')}</select></label>
        <label class="field full"><span>Notes</span>
          <textarea class="input" name="notes" rows="2" maxlength="2000">${esc(t.notes)}</textarea></label>
        <div class="field full"><span class="field-label">Receipts &amp; attachments</span>
          <div class="dropzone" id="drop">
            <div>${icon('clip')} Drag &amp; drop receipt images or PDFs here</div>
            <div class="row">
              <label class="btn sm">${icon('upload')} Choose files<input type="file" id="fileIn" accept="image/*,application/pdf" multiple hidden></label>
              <label class="btn sm">${icon('camera')} Take photo<input type="file" id="camIn" accept="image/*" capture="environment" hidden></label>
            </div>
          </div>
          <div class="receipts" id="rList"></div>
        </div>
      </div>
      <datalist id="vendorList">${[...new Set(state.transactions.map((x) => x.vendor).filter(Boolean))].slice(0, 200).map((v) => `<option value="${esc(v)}">`).join('')}</datalist>
      <button type="submit" hidden></button>`;

    const footer = `
      ${isEdit ? `<button class="btn danger left" type="button" data-act="delete">${icon('trash')} Delete</button>` : ''}
      <button class="btn" type="button" data-close>Cancel</button>
      ${isEdit ? '' : '<button class="btn" type="button" data-act="save-another">Save &amp; add another</button>'}
      <button class="btn primary" type="button" data-act="save">${isEdit ? 'Save changes' : 'Save'}</button>`;

    const m = openModal({
      title: isEdit ? 'Edit transaction' : 'New transaction',
      body, footer, size: 'lg',
      onClose: () => urls.forEach((u) => URL.revokeObjectURL(u)),
    });

    const catSel = $('[name=categoryId]', body);
    const fillCats = () => {
      const list = catsOfType(t.type);
      const sel = list.some((c) => c.id === t.categoryId) ? t.categoryId : (state.lastCategory[t.type] && state.catMap.has(state.lastCategory[t.type]) ? state.lastCategory[t.type] : '');
      catSel.innerHTML = `<option value="" disabled ${sel ? '' : 'selected'}>Select a category…</option>` + categoryOptions(t.type, sel);
      if (!list.length) catSel.innerHTML = `<option value="" disabled selected>No ${t.type} categories — add one under Categories</option>`;
    };
    fillCats();
    bindSeg(body, 'fType', (v) => {
      t.type = v;
      $('#vendorLbl', body).textContent = v === 'income' ? 'Customer / payer' : 'Vendor / payee';
      fillCats();
    });

    const renderReceipts = () => {
      const rl = $('#rList', body);
      rl.innerHTML = receipts.map((r, i) => {
        const isImg = (r.mime || '').startsWith('image/');
        return `<div class="receipt">
          <button type="button" class="thumb" data-view="${i}" title="View ${esc(r.name)}">${isImg ? `<img alt="" src="${objURL(r.blob)}">` : icon('file')}</button>
          ${r.isNew ? '<span class="badge-new">NEW</span>' : ''}
          <button type="button" class="rm" data-rm="${i}" aria-label="Remove ${esc(r.name)}">${icon('x')}</button>
          <div class="meta" title="${esc(r.name)}">${esc(r.name)}<br><span class="muted">${esc(fmtBytes(r.size))}</span></div></div>`;
      }).join('');
    };
    renderReceipts();

    const addFiles = async (files) => {
      for (const f0 of files) {
        if (!/^image\/|^application\/pdf$/.test(f0.type)) { toast(`${f0.name}: only images and PDFs can be attached`, 'error'); continue; }
        if (f0.size > 25 * 1024 * 1024) { toast(`${f0.name} is larger than 25 MB`, 'error'); continue; }
        const f = await compressImage(f0);
        receipts.push({ id: uid(), transactionId: null, name: f.name || 'receipt', mime: f.type || 'application/octet-stream', size: f.size, blob: f, createdAt: new Date().toISOString(), isNew: true });
      }
      renderReceipts();
    };
    $('#fileIn', body).addEventListener('change', (e) => { addFiles([...e.target.files]); e.target.value = ''; });
    $('#camIn', body).addEventListener('change', (e) => { addFiles([...e.target.files]); e.target.value = ''; });
    const drop = $('#drop', body);
    ['dragenter', 'dragover'].forEach((ev) => drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add('drag'); }));
    ['dragleave', 'drop'].forEach((ev) => drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.remove('drag'); }));
    drop.addEventListener('drop', (e) => addFiles([...(e.dataTransfer?.files || [])]));
    $('#rList', body).addEventListener('click', (e) => {
      const v = e.target.closest('[data-view]');
      const r = e.target.closest('[data-rm]');
      if (v) viewReceipt(receipts[Number(v.dataset.view)]);
      if (r) {
        const [gone] = receipts.splice(Number(r.dataset.rm), 1);
        if (!gone.isNew) removed.push(gone.id);
        renderReceipts();
      }
    });

    const save = async (another) => {
      const fd = new FormData(body);
      const amtIn = $('[name=amount]', body);
      const amount = parseAmount(fd.get('amount'));
      amtIn.classList.toggle('invalid', !(amount > 0));
      if (!(amount > 0)) { toast('Enter an amount greater than zero', 'error'); amtIn.focus(); return; }
      const date = fd.get('date');
      if (!/^\d{4}-\d{2}-\d{2}$/.test(date || '')) { toast('Choose a valid date', 'error'); return; }
      const categoryId = fd.get('categoryId');
      catSel.classList.toggle('invalid', !categoryId);
      if (!categoryId) { toast('Choose a category', 'error'); catSel.focus(); return; }
      const now = new Date().toISOString();
      const rec = {
        id: t.id || uid(), type: t.type, amount, date, categoryId,
        description: String(fd.get('description') || '').trim(),
        vendor: String(fd.get('vendor') || '').trim(),
        paymentMethod: String(fd.get('paymentMethod') || ''),
        notes: String(fd.get('notes') || '').trim(),
        createdAt: t.createdAt || now, updatedAt: now,
      };
      try {
        await DB.put('transactions', rec);
        const fresh = receipts.filter((r) => r.isNew).map(({ isNew, ...r }) => ({ ...r, transactionId: rec.id }));
        if (fresh.length) await DB.putMany('receipts', fresh);
        if (removed.length) await DB.delMany('receipts', removed);
      } catch (err) {
        console.error(err);
        toast(err && err.name === 'QuotaExceededError' ? 'Storage is full — remove some receipts or back up and clear old data.' : 'Could not save: ' + (err.message || err), 'error');
        return;
      }
      const i = state.transactions.findIndex((x) => x.id === rec.id);
      if (i >= 0) state.transactions[i] = rec; else state.transactions.push(rec);
      sortTx();
      state.receiptCounts.set(rec.id, receipts.length);
      state.lastCategory[rec.type] = rec.categoryId;
      m.close();
      toast(isEdit ? 'Transaction updated' : `${rec.type === 'income' ? 'Income' : 'Expense'} of ${money(rec.amount)} saved`);
      render();
      if (another) openTransactionForm(null, rec.type).then(() => {});
    };

    body.addEventListener('submit', (e) => { e.preventDefault(); save(false); });
    m.foot.addEventListener('click', async (e) => {
      const b = e.target.closest('[data-act]');
      if (!b) return;
      if (b.dataset.act === 'save') save(false);
      if (b.dataset.act === 'save-another') save(true);
      if (b.dataset.act === 'delete') {
        const ok = await confirmDialog(`Delete this ${t.type} of <b>${esc(money(t.amount))}</b>${receipts.length ? ` and its ${receipts.length} receipt${receipts.length > 1 ? 's' : ''}` : ''}? This can’t be undone.`, { title: 'Delete transaction', okLabel: 'Delete', danger: true });
        if (!ok) return;
        await DB.deleteTransactions([t.id]);
        state.transactions = state.transactions.filter((x) => x.id !== t.id);
        state.receiptCounts.delete(t.id);
        m.close();
        toast('Transaction deleted');
        render();
      }
    });
  }

  function viewReceipt(r) {
    if (!r) return;
    const url = URL.createObjectURL(r.blob);
    const isImg = (r.mime || '').startsWith('image/');
    const isPdf = r.mime === 'application/pdf';
    const m = openModal({
      title: r.name, size: 'lg',
      body: `<div class="viewer">${isImg ? `<img src="${url}" alt="Receipt ${esc(r.name)}">` : isPdf ? `<iframe src="${url}" title="${esc(r.name)}"></iframe>` : `<p>Preview not available.</p>`}</div>
        <p class="small muted" style="margin:10px 0 0">${esc(fmtBytes(r.size))} · added ${esc(fmtDateTime(r.createdAt))}</p>`,
      footer: `<a class="btn" href="${url}" target="_blank" rel="noopener">${icon('external')} Open in new tab</a>
        <a class="btn primary" href="${url}" download="${esc(r.name)}">${icon('download')} Download</a>`,
      onClose: () => setTimeout(() => URL.revokeObjectURL(url), 1000),
    });
    return m;
  }

  /* =========================================================
   * Categories
   * ======================================================= */
  function renderCategories(view) {
    const type = state.catTab;
    const ym = thisMonth();
    const y = thisYear();
    const monthBy = byCategory(txInMonth(ym));
    const yearBy = byCategory(txInYear(y));
    const allBy = byCategory(state.transactions);
    const list = catsOfType(type);

    view.innerHTML = `
      <div class="toolbar">
        ${seg('catTab', [['expense', `Expense categories (${catsOfType('expense').length})`], ['income', `Income categories (${catsOfType('income').length})`]], type)}
        <span class="spacer"></span>
        <button class="btn primary" type="button" id="addCat">${icon('plus')} Add category</button>
      </div>
      <div class="card">
        ${list.length ? `
        <div class="cat-row head"><span></span><span>Name</span>
          <span class="r hide-sm">${type === 'expense' ? 'Monthly budget' : 'Transactions'}</span>
          <span class="r">${esc(monthLabel(ym, 'short'))}</span><span class="r hide-sm">${y} total</span><span><span class="sr-only">Actions</span></span></div>
        ${list.map((c) => {
          const mm = monthBy.get(c.id), yy = yearBy.get(c.id), aa = allBy.get(c.id);
          const over = c.budget && mm && mm.total > c.budget;
          return `<div class="cat-row">
            <span class="ico" aria-hidden="true">${esc(c.icon || '🏷️')}</span>
            <span style="min-width:0"><div class="name">${esc(c.name)}</div><div class="small muted">${aa ? `${aa.count} transaction${aa.count > 1 ? 's' : ''}` : 'Not used yet'}</div></span>
            <span class="r hide-sm">${type === 'expense' ? (c.budget ? esc(money(c.budget)) : '<span class="muted">—</span>') : (yy ? yy.count : 0)}</span>
            <span class="r ${over ? 'bad' : ''}">${mm ? esc(money(mm.total)) : '<span class="muted">—</span>'}${over ? `<div class="small">${icon('alert', 'small')} over</div>` : ''}</span>
            <span class="r hide-sm">${yy ? esc(money(yy.total)) : '<span class="muted">—</span>'}</span>
            <span class="actions">
              <button class="icon-btn" type="button" data-edit="${esc(c.id)}" aria-label="Edit ${esc(c.name)}" title="Edit">${icon('edit')}</button>
              <button class="icon-btn danger" type="button" data-del="${esc(c.id)}" aria-label="Delete ${esc(c.name)}" title="Delete">${icon('trash')}</button>
            </span></div>`;
        }).join('')}` : `<div class="empty"><h3>No ${type} categories</h3><p>Add a category, or restore the standard set from Settings.</p></div>`}
      </div>`;

    bindSeg(view, 'catTab', (v) => { state.catTab = v; render(); });
    $('#addCat', view).addEventListener('click', () => openCategoryForm(null, type));
    view.addEventListener('click', (e) => {
      const ed = e.target.closest('[data-edit]');
      const dl = e.target.closest('[data-del]');
      if (ed) openCategoryForm(state.catMap.get(ed.dataset.edit));
      if (dl) deleteCategory(state.catMap.get(dl.dataset.del));
    });
  }

  function openCategoryForm(existing, presetType = 'expense') {
    const c = existing ? { ...existing } : { name: '', type: presetType, icon: '🏷️', budget: 0 };
    const body = document.createElement('form');
    body.noValidate = true;
    body.innerHTML = `
      <div class="form-grid">
        <div class="full">${seg('cType', [['expense', 'Expense'], ['income', 'Income']], c.type, 'type-toggle')}
          ${existing && (byCategory(state.transactions).get(c.id)) ? '<div class="hint small muted" style="margin-top:6px">Changing the type also changes the type of this category’s existing transactions.</div>' : ''}</div>
        <label class="field full"><span>Name</span><input class="input" name="name" maxlength="60" required value="${esc(c.name)}" placeholder="e.g. Cleaning Supplies"></label>
        <label class="field full" id="budgetField" ${c.type === 'income' ? 'hidden' : ''}><span>Monthly budget (optional)</span>
          <input class="input" name="budget" inputmode="decimal" placeholder="No budget" value="${c.budget ? esc(amountPlain(c.budget)) : ''}">
          <span class="hint">Shows progress on the dashboard and warns when spending goes over.</span></label>
        <div class="field full"><span class="field-label">Icon</span>
          <div class="icon-picker" id="iconPick">${ICON_CHOICES.map((ic) => `<button type="button" data-ic="${esc(ic)}" class="${ic === c.icon ? 'active' : ''}" aria-label="Icon ${esc(ic)}">${esc(ic)}</button>`).join('')}</div>
        </div>
      </div><button type="submit" hidden></button>`;
    const m = openModal({
      title: existing ? 'Edit category' : 'New category', body, size: 'sm',
      footer: `<button class="btn" type="button" data-close>Cancel</button><button class="btn primary" type="button" data-act="save">${existing ? 'Save' : 'Add category'}</button>`,
    });
    bindSeg(body, 'cType', (v) => { c.type = v; $('#budgetField', body).hidden = v === 'income'; });
    $('#iconPick', body).addEventListener('click', (e) => {
      const b = e.target.closest('[data-ic]');
      if (!b) return;
      c.icon = b.dataset.ic;
      $$('#iconPick button', body).forEach((x) => x.classList.toggle('active', x === b));
    });
    const save = async () => {
      const name = $('[name=name]', body).value.trim().replace(/\s+/g, ' ');
      if (!name) { toast('Enter a category name', 'error'); return; }
      if (state.categories.some((x) => x.id !== c.id && x.type === c.type && x.name.toLowerCase() === name.toLowerCase())) {
        toast(`A ${c.type} category named “${name}” already exists`, 'error'); return;
      }
      let budget = 0;
      const bIn = $('[name=budget]', body).value.trim();
      if (c.type === 'expense' && bIn) {
        budget = parseAmount(bIn);
        if (!(budget >= 0)) { toast('Budget must be a number', 'error'); return; }
      }
      const rec = { id: c.id || uid(), name, type: c.type, icon: c.icon || '🏷️', budget: c.type === 'expense' ? budget : 0, createdAt: c.createdAt || new Date().toISOString() };
      await DB.put('categories', rec);
      if (existing && existing.type !== rec.type) {
        const changed = state.transactions.filter((t) => t.categoryId === rec.id).map((t) => ({ ...t, type: rec.type, updatedAt: new Date().toISOString() }));
        if (changed.length) {
          await DB.putMany('transactions', changed);
          const map = new Map(changed.map((t) => [t.id, t]));
          state.transactions = state.transactions.map((t) => map.get(t.id) || t);
        }
      }
      const i = state.categories.findIndex((x) => x.id === rec.id);
      if (i >= 0) state.categories[i] = rec; else state.categories.push(rec);
      indexCategories();
      state.catTab = rec.type;
      m.close();
      toast(existing ? 'Category updated' : `Category “${name}” added`);
      render();
    };
    body.addEventListener('submit', (e) => { e.preventDefault(); save(); });
    $('[data-act=save]', m.el).addEventListener('click', save);
  }

  async function deleteCategory(c) {
    if (!c) return;
    const used = state.transactions.filter((t) => t.categoryId === c.id);
    if (!used.length) {
      const ok = await confirmDialog(`Delete the category <b>${esc(c.name)}</b>?`, { title: 'Delete category', okLabel: 'Delete', danger: true });
      if (!ok) return;
      await DB.del('categories', c.id);
      state.categories = state.categories.filter((x) => x.id !== c.id);
      indexCategories();
      toast('Category deleted');
      render();
      return;
    }
    const others = catsOfType(c.type).filter((x) => x.id !== c.id);
    const total = used.reduce((s, t) => s + t.amount, 0);
    const m = openModal({
      title: `Delete “${c.name}”`, size: 'sm',
      body: `<p><b>${used.length}</b> transaction${used.length > 1 ? 's' : ''} (${esc(money(total))}) use this category. What should happen to ${used.length > 1 ? 'them' : 'it'}?</p>
        <div class="stack" style="gap:10px">
          ${others.length ? `<label class="check"><input type="radio" name="mode" value="move" checked> Move them to another category</label>
          <select class="input" id="moveTo">${others.map((o) => `<option value="${esc(o.id)}">${esc(o.icon || '')} ${esc(o.name)}</option>`).join('')}</select>` : ''}
          <label class="check"><input type="radio" name="mode" value="delete" ${others.length ? '' : 'checked'}> Delete the transactions and their receipts</label>
        </div>`,
      footer: `<button class="btn" type="button" data-close>Cancel</button><button class="btn danger solid" type="button" data-act="go">Delete category</button>`,
    });
    $('[data-act=go]', m.el).addEventListener('click', async () => {
      const mode = $('input[name=mode]:checked', m.el).value;
      if (mode === 'move') {
        const to = $('#moveTo', m.el).value;
        if (!to || to === c.id) { toast('Pick a category to move to', 'error'); return; }
        const now = new Date().toISOString();
        const moved = used.map((t) => ({ ...t, categoryId: to, updatedAt: now }));
        await DB.putMany('transactions', moved);
        const map = new Map(moved.map((t) => [t.id, t]));
        state.transactions = state.transactions.map((t) => map.get(t.id) || t);
      } else {
        await DB.deleteTransactions(used.map((t) => t.id));
        const ids = new Set(used.map((t) => t.id));
        state.transactions = state.transactions.filter((t) => !ids.has(t.id));
        ids.forEach((id) => state.receiptCounts.delete(id));
      }
      await DB.del('categories', c.id);
      state.categories = state.categories.filter((x) => x.id !== c.id);
      indexCategories();
      m.close();
      toast(mode === 'move' ? `Category deleted — ${used.length} transaction${used.length > 1 ? 's' : ''} moved` : 'Category and its transactions deleted');
      render();
    });
  }

  /* =========================================================
   * Reports
   * ======================================================= */
  function renderReports(view) {
    const r = state.report;
    r.month = r.month || thisMonth();
    r.year = r.year || thisYear();
    const label = r.tab === 'monthly' ? monthLabel(r.month) : r.tab === 'annual' ? String(r.year) : 'All years';
    view.innerHTML = `
      <div class="toolbar">
        ${seg('repTab', [['monthly', 'Monthly'], ['annual', 'Annual'], ['yoy', 'Year over year']], r.tab)}
        ${r.tab !== 'yoy' ? periodNav(label, 'repPeriod') : ''}
        <span class="spacer"></span>
        <button class="btn" type="button" id="repCsv">${icon('download')} Export CSV</button>
        <button class="btn" type="button" id="repPrint">${icon('print')} Print / PDF</button>
      </div>
      <div class="print-only" style="margin-bottom:12px"><h2>${esc(state.settings.businessName)} — ${esc(r.tab === 'monthly' ? 'Monthly report' : r.tab === 'annual' ? 'Annual report' : 'Year-over-year report')}: ${esc(label)}</h2>
        <div class="small">Generated ${esc(fmtDateTime(new Date().toISOString()))}</div></div>
      <div id="repBody" class="stack"></div>`;
    bindSeg(view, 'repTab', (v) => { r.tab = v; render(); });
    const pn = $('#repPeriod', view);
    if (pn) pn.addEventListener('click', (e) => {
      const b = e.target.closest('[data-step]');
      if (!b) return;
      if (r.tab === 'monthly') r.month = addMonths(r.month, Number(b.dataset.step)); else r.year += Number(b.dataset.step);
      render();
    });
    $('#repPrint', view).addEventListener('click', () => window.print());
    const body = $('#repBody', view);
    let csv;
    if (r.tab === 'monthly') csv = reportMonthly(body, r.month);
    else if (r.tab === 'annual') csv = reportAnnual(body, r.year);
    else csv = reportYoY(body);
    $('#repCsv', view).addEventListener('click', () => {
      download(`${slug(state.settings.businessName)}-${r.tab}-report-${slug(label)}.csv`, toCSV(csv()), 'text/csv');
    });
  }

  function summaryTiles(t, extra = '') {
    return `<div class="kpis">
      <div class="card kpi"><div class="label"><span class="swatch" style="background:var(--series-income)"></span>Total income</div><div class="value">${esc(money(t.income))}</div></div>
      <div class="card kpi"><div class="label"><span class="swatch" style="background:var(--series-expense)"></span>Total expenses</div><div class="value">${esc(money(t.expense))}</div></div>
      <div class="card kpi"><div class="label">Net profit</div><div class="value ${t.net < 0 ? 'bad' : ''}">${esc(signedMoney(t.net))}</div>
        <div class="meta">${t.income ? `${pct(t.net, t.income)}% profit margin` : '—'}</div></div>
      ${extra}
    </div>`;
  }

  function categoryTable(title, type, txs, grand) {
    const bc = byCategory(txs.filter((t) => t.type === type));
    const rows = [...bc.entries()].map(([id, v]) => ({ c: catOf(id), ...v })).sort((a, b) => b.total - a.total);
    const showBudget = type === 'expense' && rows.some((r) => r.c.budget);
    if (!rows.length) return `<div class="card"><div class="card-head"><h2>${esc(title)}</h2></div><p class="muted" style="margin:0">None recorded.</p></div>`;
    return `<div class="card"><div class="card-head"><h2>${esc(title)}</h2><span class="sub">${rows.length} categor${rows.length === 1 ? 'y' : 'ies'}</span></div>
      <div class="table-wrap"><table class="data">
        <thead><tr><th>Category</th><th class="r">Count</th><th class="r">Amount</th><th class="r">% of total</th>${showBudget ? '<th class="r">Budget</th><th class="r">Remaining</th>' : ''}</tr></thead>
        <tbody>${rows.map((r) => {
          const rem = r.c.budget ? r.c.budget - r.total : null;
          return `<tr><td><span class="cat-cell"><span aria-hidden="true">${esc(r.c.icon || '')}</span>${esc(r.c.name)}</span></td>
            <td class="r">${r.count}</td><td class="r">${esc(money(r.total))}</td><td class="r">${pct(r.total, grand)}%</td>
            ${showBudget ? `<td class="r">${r.c.budget ? esc(money(r.c.budget)) : '—'}</td><td class="r ${rem != null && rem < 0 ? 'bad' : ''}">${rem != null ? esc(signedMoney(rem)) : '—'}</td>` : ''}</tr>`;
        }).join('')}
        <tr class="total"><td>Total</td><td class="r">${rows.reduce((s, r) => s + r.count, 0)}</td><td class="r">${esc(money(grand))}</td><td class="r">100%</td>${showBudget ? '<td></td><td></td>' : ''}</tr>
        </tbody></table></div></div>`;
  }

  function reportMonthly(body, ym) {
    const txs = txInMonth(ym);
    const t = totals(txs);
    const [yy, mm] = ym.split('-').map(Number);
    const daysInMonth = new Date(yy, mm, 0).getDate();
    const elapsed = ym === thisMonth() ? Number(todayISO().slice(8)) : ym < thisMonth() ? daysInMonth : 0;
    const avgDaily = elapsed ? Math.round(t.expense / elapsed) : 0;
    const days = new Map();
    for (const x of txs) {
      const d = days.get(x.date) || { income: 0, expense: 0, count: 0 };
      d[x.type] += x.amount; d.count++;
      days.set(x.date, d);
    }
    const dayRows = [...days.entries()].sort((a, b) => a[0].localeCompare(b[0]));

    body.innerHTML = summaryTiles(t, `<div class="card kpi"><div class="label">Avg. daily spending</div><div class="value">${esc(money(avgDaily))}</div><div class="meta">${t.count} transactions</div></div>`) +
      `<div class="grid cols-2">${categoryTable('Income by category', 'income', txs, t.income)}${categoryTable('Expenses by category', 'expense', txs, t.expense)}</div>
      <div class="card"><div class="card-head"><h2>Daily breakdown</h2><span class="sub">${dayRows.length} day${dayRows.length === 1 ? '' : 's'} with activity</span></div>
      ${dayRows.length ? `<div class="table-wrap"><table class="data">
        <thead><tr><th>Date</th><th class="r">Transactions</th><th class="r">Income</th><th class="r">Expenses</th><th class="r">Net</th></tr></thead>
        <tbody>${dayRows.map(([d, v]) => `<tr><td>${esc(fmtDate(d, { weekday: 'short', day: 'numeric', month: 'short' }))}</td><td class="r">${v.count}</td>
          <td class="r ${v.income ? '' : 'zero'}">${esc(money(v.income))}</td><td class="r ${v.expense ? '' : 'zero'}">${esc(money(v.expense))}</td>
          <td class="r ${v.income - v.expense < 0 ? 'bad' : ''}">${esc(signedMoney(v.income - v.expense))}</td></tr>`).join('')}
        <tr class="total"><td>Total</td><td class="r">${t.count}</td><td class="r">${esc(money(t.income))}</td><td class="r">${esc(money(t.expense))}</td><td class="r">${esc(signedMoney(t.net))}</td></tr>
        </tbody></table></div>` : '<p class="muted" style="margin:0">No transactions this month.</p>'}</div>`;

    return () => {
      const rows = [[`${state.settings.businessName} — Monthly report`, monthLabel(ym)], [],
        ['Summary'], ['Total income', amountPlain(t.income)], ['Total expenses', amountPlain(t.expense)], ['Net profit', amountPlain(t.net)], [],
        ['Type', 'Category', 'Transactions', 'Amount', '% of type total']];
      for (const type of ['income', 'expense']) {
        const grand = type === 'income' ? t.income : t.expense;
        [...byCategory(txs.filter((x) => x.type === type)).entries()].sort((a, b) => b[1].total - a[1].total)
          .forEach(([id, v]) => rows.push([type === 'income' ? 'Income' : 'Expense', catOf(id).name, v.count, amountPlain(v.total), pct(v.total, grand)]));
      }
      rows.push([], ['Date', 'Transactions', 'Income', 'Expenses', 'Net']);
      dayRows.forEach(([d, v]) => rows.push([d, v.count, amountPlain(v.income), amountPlain(v.expense), amountPlain(v.income - v.expense)]));
      return rows;
    };
  }

  function reportAnnual(body, year) {
    const txs = txInYear(year);
    const t = totals(txs);
    const series = monthlySeries(year);
    const activeMonths = series.filter((s) => s.income || s.expense).length;

    // matrix: category -> [12 months]
    const matrix = (type) => {
      const m = new Map();
      for (const x of txs) {
        if (x.type !== type) continue;
        if (!m.has(x.categoryId)) m.set(x.categoryId, Array(12).fill(0));
        m.get(x.categoryId)[Number(x.date.slice(5, 7)) - 1] += x.amount;
      }
      return [...m.entries()].map(([id, arr]) => ({ c: catOf(id), arr, total: arr.reduce((a, b) => a + b, 0) })).sort((a, b) => b.total - a.total);
    };
    const inc = matrix('income'), exp = matrix('expense');
    const cell = (v) => `<td class="r ${v ? '' : 'zero'}">${v ? esc(money(v)) : '–'}</td>`;
    const months = Array.from({ length: 12 }, (_, i) => monthShort(i));
    const sectionRows = (title, rows, key) => `
      <tr class="section"><td class="sticky-col" colspan="14">${esc(title)}</td></tr>
      ${rows.map((r) => `<tr><td class="sticky-col"><span class="cat-cell"><span aria-hidden="true">${esc(r.c.icon || '')}</span>${esc(r.c.name)}</span></td>${r.arr.map(cell).join('')}<td class="r"><b>${esc(money(r.total))}</b></td></tr>`).join('')}
      <tr class="total"><td class="sticky-col">Total ${esc(title.toLowerCase())}</td>${series.map((s) => cell(s[key])).join('')}<td class="r">${esc(money(t[key]))}</td></tr>`;

    body.innerHTML = summaryTiles(t, `<div class="card kpi"><div class="label">Avg. monthly expenses</div><div class="value">${esc(money(activeMonths ? Math.round(t.expense / activeMonths) : 0))}</div><div class="meta">over ${activeMonths} active month${activeMonths === 1 ? '' : 's'}</div></div>`) +
      `<div class="card"><div class="card-head"><h2>Monthly totals <span class="sub">· ${year}</span></h2>
        <div class="legend"><span><i class="swatch" style="background:var(--series-income)"></i>Income</span><span><i class="swatch" style="background:var(--series-expense)"></i>Expenses</span></div></div>
        <div id="annualChart"></div></div>
      <div class="card"><div class="card-head"><h2>Category by month <span class="sub">· ${year}</span></h2></div>
      ${txs.length ? `<div class="table-wrap"><table class="data">
        <thead><tr><th class="sticky-col">Category</th>${months.map((mn) => `<th class="r">${esc(mn)}</th>`).join('')}<th class="r">Total</th></tr></thead>
        <tbody>
          ${sectionRows('Income', inc, 'income')}
          ${sectionRows('Expenses', exp, 'expense')}
          <tr class="net"><td class="sticky-col">Net profit</td>${series.map((s) => `<td class="r ${s.income - s.expense < 0 ? 'bad' : ''}">${s.income || s.expense ? esc(signedMoney(s.income - s.expense)) : '–'}</td>`).join('')}<td class="r ${t.net < 0 ? 'bad' : ''}">${esc(signedMoney(t.net))}</td></tr>
        </tbody></table></div>` : '<p class="muted" style="margin:0">No transactions this year.</p>'}</div>`;

    onCleanup(Charts.groupedBars($('#annualChart', body), {
      labels: months,
      fullLabels: Array.from({ length: 12 }, (_, i) => `${monthLong(i)} ${year}`),
      series: [
        { name: 'Income', colorVar: '--series-income', values: series.map((s) => s.income) },
        { name: 'Expenses', colorVar: '--series-expense', values: series.map((s) => s.expense) },
      ],
      format: money, compact: moneyCompact, ariaLabel: `Monthly totals for ${year}`,
      footer: (i) => { const n = series[i].income - series[i].expense; return `<div class="tip-foot"><span>Net</span><b class="${n < 0 ? 'bad' : ''}">${esc(signedMoney(n))}</b></div>`; },
      onSelect: (i) => { state.report.tab = 'monthly'; state.report.month = `${year}-${pad(i + 1)}`; render(); },
    }));

    return () => {
      const head = ['Category', ...Array.from({ length: 12 }, (_, i) => monthLong(i)), 'Total'];
      const rows = [[`${state.settings.businessName} — Annual report`, String(year)], [], ['Income'], head];
      inc.forEach((r) => rows.push([r.c.name, ...r.arr.map(amountPlain), amountPlain(r.total)]));
      rows.push(['Total income', ...series.map((s) => amountPlain(s.income)), amountPlain(t.income)], [], ['Expenses'], head);
      exp.forEach((r) => rows.push([r.c.name, ...r.arr.map(amountPlain), amountPlain(r.total)]));
      rows.push(['Total expenses', ...series.map((s) => amountPlain(s.expense)), amountPlain(t.expense)], []);
      rows.push(['Net profit', ...series.map((s) => amountPlain(s.income - s.expense)), amountPlain(t.net)]);
      return rows;
    };
  }

  function reportYoY(body) {
    const years = yearsWithData();
    const data = years.map((y) => {
      const t = totals(txInYear(y));
      const bc = [...byCategory(txInYear(y).filter((x) => x.type === 'expense')).entries()].sort((a, b) => b[1].total - a[1].total);
      return { y, ...t, top: bc[0] ? catOf(bc[0][0]).name : '—' };
    });
    const change = (cur, prev) => (prev ? `${cur >= prev ? '▲' : '▼'} ${Math.abs(((cur - prev) / Math.abs(prev)) * 100).toFixed(1)}%` : '—');
    body.innerHTML = `
      <div class="card"><div class="card-head"><h2>Totals by year</h2>
        <div class="legend"><span><i class="swatch" style="background:var(--series-income)"></i>Income</span><span><i class="swatch" style="background:var(--series-expense)"></i>Expenses</span></div></div>
        <div id="yoyChart"></div></div>
      <div class="card"><div class="table-wrap"><table class="data">
        <thead><tr><th>Year</th><th class="r">Income</th><th class="r">Change</th><th class="r">Expenses</th><th class="r">Change</th><th class="r">Net profit</th><th class="r">Margin</th><th class="r">Transactions</th><th>Largest expense</th></tr></thead>
        <tbody>${data.slice().reverse().map((d) => {
          const prev = data.find((x) => x.y === d.y - 1);
          return `<tr><td><button class="link-btn" type="button" data-year="${d.y}">${d.y}</button></td>
            <td class="r">${esc(money(d.income))}</td><td class="r muted">${prev ? change(d.income, prev.income) : '—'}</td>
            <td class="r">${esc(money(d.expense))}</td><td class="r muted">${prev ? change(d.expense, prev.expense) : '—'}</td>
            <td class="r ${d.net < 0 ? 'bad' : ''}">${esc(signedMoney(d.net))}</td><td class="r">${d.income ? pct(d.net, d.income) + '%' : '—'}</td>
            <td class="r">${d.count}</td><td>${esc(d.top)}</td></tr>`;
        }).join('')}</tbody></table></div></div>`;
    onCleanup(Charts.groupedBars($('#yoyChart', body), {
      labels: data.map((d) => String(d.y)),
      series: [
        { name: 'Income', colorVar: '--series-income', values: data.map((d) => d.income) },
        { name: 'Expenses', colorVar: '--series-expense', values: data.map((d) => d.expense) },
      ],
      format: money, compact: moneyCompact, ariaLabel: 'Income and expenses by year',
      footer: (i) => `<div class="tip-foot"><span>Net</span><b class="${data[i].net < 0 ? 'bad' : ''}">${esc(signedMoney(data[i].net))}</b></div>`,
      onSelect: (i) => { state.report.tab = 'annual'; state.report.year = data[i].y; render(); },
    }));
    body.addEventListener('click', (e) => {
      const b = e.target.closest('[data-year]');
      if (b) { state.report.tab = 'annual'; state.report.year = Number(b.dataset.year); render(); }
    });
    return () => [[`${state.settings.businessName} — Year over year`], [], ['Year', 'Income', 'Expenses', 'Net profit', 'Margin %', 'Transactions', 'Largest expense category'],
      ...data.map((d) => [d.y, amountPlain(d.income), amountPlain(d.expense), amountPlain(d.net), d.income ? pct(d.net, d.income) : '', d.count, d.top])];
  }

  /* =========================================================
   * Backup & restore
   * ======================================================= */
  async function createBackup(includeReceipts = true) {
    try {
      const receipts = includeReceipts ? await DB.getAll('receipts') : [];
      const out = {
        app: 'ledgerly', version: 1, exportedAt: new Date().toISOString(),
        settings: { ...state.settings, lastBackupAt: undefined },
        categories: state.categories,
        transactions: state.transactions,
        receipts: [],
        includesReceipts: includeReceipts,
      };
      for (const r of receipts) {
        const { blob, ...meta } = r;
        out.receipts.push({ ...meta, data: await blobToDataURL(blob) });
      }
      const stamp = new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-');
      download(`${slug(state.settings.businessName)}-backup-${stamp}${includeReceipts ? '' : '-no-receipts'}.json`, JSON.stringify(out), 'application/json');
      await saveSettings({ lastBackupAt: new Date().toISOString() });
      toast(`Backup downloaded${includeReceipts ? ` with ${receipts.length} receipt${receipts.length === 1 ? '' : 's'}` : ''}`);
      if (['dashboard', 'backup'].includes(currentRoute())) render();
    } catch (err) {
      console.error(err);
      toast('Backup failed: ' + (err.message || err), 'error');
    }
  }

  const isISODate = (s) => typeof s === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(s);
  function validateBackup(data) {
    if (!data || typeof data !== 'object' || data.app !== 'ledgerly') throw new Error('This file is not a Ledgerly backup.');
    if (!Array.isArray(data.categories) || !Array.isArray(data.transactions)) throw new Error('Backup file is missing categories or transactions.');
    const str = (v, max = 2000) => String(v ?? '').slice(0, max);
    const categories = data.categories.filter((c) => c && c.id && c.name).map((c) => ({
      id: str(c.id, 100), name: str(c.name, 60), type: c.type === 'income' ? 'income' : 'expense',
      icon: str(c.icon || '🏷️', 16), budget: Number.isInteger(c.budget) && c.budget > 0 ? c.budget : 0, createdAt: str(c.createdAt, 40) || new Date().toISOString(),
    }));
    const transactions = data.transactions.filter((t) => t && t.id && isISODate(t.date) && Number.isFinite(Number(t.amount))).map((t) => ({
      id: str(t.id, 100), type: t.type === 'income' ? 'income' : 'expense', categoryId: str(t.categoryId, 100),
      amount: Math.abs(Math.round(Number(t.amount))), date: t.date, description: str(t.description, 200), vendor: str(t.vendor, 120),
      paymentMethod: str(t.paymentMethod, 60), notes: str(t.notes), createdAt: str(t.createdAt, 40), updatedAt: str(t.updatedAt, 40),
    }));
    const receipts = (Array.isArray(data.receipts) ? data.receipts : []).filter((r) => r && r.id && r.transactionId && r.data).map((r) => {
      const blob = dataURLToBlob(r.data);
      return { id: str(r.id, 100), transactionId: str(r.transactionId, 100), name: str(r.name || 'receipt', 200), mime: blob.type, size: blob.size, blob, createdAt: str(r.createdAt, 40) || new Date().toISOString() };
    });
    const settings = { ...DEFAULT_SETTINGS };
    if (data.settings && typeof data.settings === 'object') {
      if (data.settings.businessName) settings.businessName = str(data.settings.businessName, 80);
      if (CURRENCIES.some(([c]) => c === data.settings.currency)) settings.currency = data.settings.currency;
      if (['system', 'light', 'dark'].includes(data.settings.theme)) settings.theme = data.settings.theme;
      if (typeof data.settings.compressReceipts === 'boolean') settings.compressReceipts = data.settings.compressReceipts;
      if ([0, 1, 7, 30].includes(data.settings.backupReminderDays)) settings.backupReminderDays = data.settings.backupReminderDays;
    }
    return { settings, categories, transactions, receipts, exportedAt: data.exportedAt };
  }

  async function restoreBackup(parsed, mode) {
    if (mode === 'replace') {
      parsed.settings.lastBackupAt = new Date().toISOString();
      await DB.replaceAll(parsed);
    } else {
      // Merge: add anything not already present. Categories with the same name+type are unified.
      const existingCat = new Map(state.categories.map((c) => [`${c.type}|${c.name.toLowerCase()}`, c.id]));
      const catIds = new Set(state.categories.map((c) => c.id));
      const remap = new Map();
      const newCats = [];
      for (const c of parsed.categories) {
        const key = `${c.type}|${c.name.toLowerCase()}`;
        if (catIds.has(c.id)) continue;
        if (existingCat.has(key)) { remap.set(c.id, existingCat.get(key)); continue; }
        newCats.push(c); catIds.add(c.id); existingCat.set(key, c.id);
      }
      const txIds = new Set(state.transactions.map((t) => t.id));
      const newTx = parsed.transactions.filter((t) => !txIds.has(t.id)).map((t) => ({ ...t, categoryId: remap.get(t.categoryId) || t.categoryId }));
      const newTxIds = new Set(newTx.map((t) => t.id));
      const newReceipts = parsed.receipts.filter((r) => newTxIds.has(r.transactionId));
      await DB.putMany('categories', newCats);
      await DB.putMany('transactions', newTx);
      if (newReceipts.length) await DB.putMany('receipts', newReceipts);
      parsed.mergeStats = { categories: newCats.length, transactions: newTx.length, receipts: newReceipts.length };
    }
    await loadAll();
    return parsed.mergeStats;
  }

  function renderBackup(view) {
    const recTotal = [...state.receiptCounts.values()].reduce((a, b) => a + b, 0);
    const last = state.settings.lastBackupAt;
    view.innerHTML = `
      <div class="stack">
        ${backupIsStale() ? `<div class="banner">${icon('alert')}<p>${last ? `Your last backup was on <b>${esc(fmtDate(last.slice(0, 10)))}</b>.` : 'You haven’t made a backup yet.'} Your data is stored only in this browser — clearing site data or losing this device would erase it.</p></div>` : ''}
        <div class="card">
          <div class="card-head"><h2>Create a backup</h2><span class="sub">${last ? `Last backup: ${esc(fmtDateTime(last))}` : 'No backups yet'}</span></div>
          <div class="stat-list">
            <div><b>${state.categories.length}</b><span>Categories</span></div>
            <div><b>${state.transactions.length}</b><span>Transactions</span></div>
            <div><b>${recTotal}</b><span>Receipts</span></div>
            <div><b id="recSize">…</b><span>Receipt storage</span></div>
          </div>
          <p class="muted" style="margin-top:0">Downloads a single <b>.json</b> file with your settings, categories, transactions and receipt files. Keep it on a USB drive, cloud storage or email it to yourself.</p>
          <div class="row">
            <button class="btn primary" type="button" id="bkFull">${icon('download')} Download full backup</button>
            <button class="btn" type="button" id="bkLite">${icon('download')} Backup without receipts</button>
            <button class="btn" type="button" id="bkCsv">${icon('download')} Export all transactions (CSV)</button>
          </div>
        </div>

        <div class="card">
          <div class="card-head"><h2>Restore from a backup</h2></div>
          <label class="dropzone" id="rsDrop" style="display:block;cursor:pointer">
            ${icon('upload')} <b>Choose a backup file</b> or drop it here
            <input type="file" id="rsFile" accept=".json,application/json" hidden>
          </label>
          <div id="rsPreview" style="margin-top:14px"></div>
        </div>
      </div>`;

    DB.receiptIndex().then((list) => {
      const el = $('#recSize', view);
      if (el) el.textContent = fmtBytes(list.reduce((s, r) => s + (r.size || 0), 0));
    });
    $('#bkFull', view).addEventListener('click', () => createBackup(true));
    $('#bkLite', view).addEventListener('click', () => createBackup(false));
    $('#bkCsv', view).addEventListener('click', () => {
      if (!state.transactions.length) return toast('No transactions to export', 'error');
      download(`${slug(state.settings.businessName)}-transactions-${todayISO()}.csv`, transactionsCSV(state.transactions), 'text/csv');
    });

    const handleFile = async (file) => {
      const prev = $('#rsPreview', view);
      if (!file) return;
      prev.innerHTML = '<p class="muted">Reading backup…</p>';
      let parsed;
      try {
        parsed = validateBackup(JSON.parse(await file.text()));
      } catch (err) {
        prev.innerHTML = `<div class="banner">${icon('alert')}<p><b>Can’t restore this file.</b> ${esc(err.message || 'It is not valid JSON.')}</p></div>`;
        return;
      }
      prev.innerHTML = `
        <p><b>${esc(file.name)}</b> — ${esc(parsed.settings.businessName)}${parsed.exportedAt ? `, created ${esc(fmtDateTime(parsed.exportedAt))}` : ''}</p>
        <div class="stat-list">
          <div><b>${parsed.categories.length}</b><span>Categories</span></div>
          <div><b>${parsed.transactions.length}</b><span>Transactions</span></div>
          <div><b>${parsed.receipts.length}</b><span>Receipts</span></div>
        </div>
        <div class="radio-cards" style="margin-bottom:14px">
          <label><input type="radio" name="rsMode" value="replace" checked><span><b>Replace all data</b><span>Erase everything here and load the backup exactly.</span></span></label>
          <label><input type="radio" name="rsMode" value="merge"><span><b>Merge</b><span>Keep current data and add anything from the backup that isn’t already here.</span></span></label>
        </div>
        <button class="btn primary" type="button" id="rsGo">${icon('upload')} Restore</button>`;
      $('#rsGo', prev).addEventListener('click', async () => {
        const mode = $('input[name=rsMode]:checked', prev).value;
        if (mode === 'replace') {
          const ok = await confirmDialog(`This will permanently replace your current <b>${state.transactions.length}</b> transactions and <b>${state.categories.length}</b> categories with the backup. Consider downloading a backup of the current data first.`,
            { title: 'Replace all data?', okLabel: 'Replace everything', danger: true });
          if (!ok) return;
        }
        try {
          const stats = await restoreBackup(parsed, mode);
          toast(mode === 'replace' ? 'Backup restored' : `Merged ${stats.transactions} transactions, ${stats.categories} categories, ${stats.receipts} receipts`);
          go('dashboard');
        } catch (err) {
          console.error(err);
          toast('Restore failed: ' + (err.message || err), 'error');
        }
      });
    };
    $('#rsFile', view).addEventListener('change', (e) => handleFile(e.target.files[0]));
    const dz = $('#rsDrop', view);
    ['dragenter', 'dragover'].forEach((ev) => dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.add('drag'); }));
    ['dragleave', 'drop'].forEach((ev) => dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.remove('drag'); }));
    dz.addEventListener('drop', (e) => handleFile(e.dataTransfer?.files?.[0]));
  }

  /* =========================================================
   * Settings
   * ======================================================= */
  function renderSettings(view) {
    const s = state.settings;
    view.innerHTML = `
      <div class="stack" style="max-width:860px">
        <div class="card"><div class="card-head"><h2>Business</h2></div>
          <div class="settings-list">
            <div class="setting"><div class="txt"><b>Business name</b><span>Shown in the menu, reports and backup file names.</span></div>
              <div class="ctl"><input class="input" id="sName" maxlength="80" value="${esc(s.businessName)}"></div></div>
            <div class="setting"><div class="txt"><b>Currency</b><span>Used to format all amounts. Changing it does not convert existing values.</span></div>
              <div class="ctl"><select class="input" id="sCur">${CURRENCIES.map(([c, n]) => `<option value="${c}" ${c === s.currency ? 'selected' : ''}>${c} — ${esc(n)}</option>`).join('')}</select></div></div>
          </div></div>

        <div class="card"><div class="card-head"><h2>Preferences</h2></div>
          <div class="settings-list">
            <div class="setting"><div class="txt"><b>Appearance</b><span>Follow your device, or force light or dark.</span></div>
              <div class="ctl">${seg('sTheme', [['system', 'System'], ['light', 'Light'], ['dark', 'Dark']], s.theme)}</div></div>
            <div class="setting"><div class="txt"><b>Compress photo receipts</b><span>Shrinks large phone photos (to max 2000px JPEG) to save space and keep backups small.</span></div>
              <div class="ctl"><label class="check"><input type="checkbox" id="sComp" ${s.compressReceipts ? 'checked' : ''}> Enabled</label></div></div>
            <div class="setting"><div class="txt"><b>Backup reminder</b><span>Show a reminder when your last backup is older than this.</span></div>
              <div class="ctl"><select class="input" id="sRem">${[[0, 'Off'], [1, 'Daily'], [7, 'Weekly'], [30, 'Monthly']].map(([v, l]) => `<option value="${v}" ${Number(s.backupReminderDays) === v ? 'selected' : ''}>${l}</option>`).join('')}</select></div></div>
          </div></div>

        <div class="card"><div class="card-head"><h2>Storage</h2></div>
          <div class="settings-list">
            <div class="setting"><div class="txt"><b>Space used</b><span id="stUsage">Checking…</span></div></div>
            <div class="setting"><div class="txt"><b>Persistent storage</b><span id="stPersist">Ask the browser not to clear this app’s data automatically when space runs low.</span></div>
              <div class="ctl"><button class="btn" type="button" id="stPersistBtn">Request</button></div></div>
          </div></div>

        <div class="card"><div class="card-head"><h2>Categories &amp; data</h2></div>
          <div class="settings-list">
            <div class="setting"><div class="txt"><b>Restore standard categories</b><span>Adds back any of the ${DEFAULT_CATEGORIES.length} default business categories you’ve removed. Your own categories are kept.</span></div>
              <div class="ctl"><button class="btn" type="button" id="dRestoreCats">Restore defaults</button></div></div>
            <div class="setting"><div class="txt"><b>Sample data</b><span>Fill the app with six months of example transactions to explore the reports.${state.transactions.length ? ' Only available when there are no transactions.' : ''}</span></div>
              <div class="ctl"><button class="btn" type="button" id="dSample" ${state.transactions.length ? 'disabled' : ''}>Load sample data</button></div></div>
          </div></div>

        <div class="card danger-zone"><div class="card-head"><h2>Danger zone</h2></div>
          <div class="settings-list">
            <div class="setting"><div class="txt"><b>Erase all data</b><span>Deletes every transaction, receipt and category on this device and resets settings. Download a backup first.</span></div>
              <div class="ctl"><button class="btn danger solid" type="button" id="dErase">${icon('trash')} Erase everything</button></div></div>
          </div></div>

        <p class="small muted">Ledgerly v1.0 · Works offline · Your data never leaves this device unless you export it. Press <b>N</b> anywhere to add a transaction.</p>
      </div>`;

    $('#sName', view).addEventListener('change', async (e) => { await saveSettings({ businessName: e.target.value.trim() || 'My Business' }); toast('Business name saved'); });
    $('#sCur', view).addEventListener('change', async (e) => { await saveSettings({ currency: e.target.value }); toast('Currency updated'); });
    bindSeg(view, 'sTheme', (v) => saveSettings({ theme: v }));
    $('#sComp', view).addEventListener('change', (e) => saveSettings({ compressReceipts: e.target.checked }));
    $('#sRem', view).addEventListener('change', (e) => saveSettings({ backupReminderDays: Number(e.target.value) }));

    if (navigator.storage && navigator.storage.estimate) {
      navigator.storage.estimate().then(({ usage, quota }) => {
        const el = $('#stUsage', view);
        if (el) el.textContent = `${fmtBytes(usage || 0)} used of about ${fmtBytes(quota || 0)} available to this app.`;
      });
    } else $('#stUsage', view).textContent = 'Not reported by this browser.';
    const pBtn = $('#stPersistBtn', view);
    if (navigator.storage && navigator.storage.persisted) {
      navigator.storage.persisted().then((p) => { if (p) { pBtn.disabled = true; pBtn.textContent = 'Enabled'; } });
      pBtn.addEventListener('click', async () => {
        const ok = await navigator.storage.persist();
        toast(ok ? 'Persistent storage enabled' : 'The browser declined — install the app or bookmark it and try again', ok ? 'ok' : 'error');
        if (ok) { pBtn.disabled = true; pBtn.textContent = 'Enabled'; }
      });
    } else pBtn.disabled = true;

    $('#dRestoreCats', view).addEventListener('click', async () => {
      const added = await seedCategories(false);
      toast(added ? `${added} categor${added === 1 ? 'y' : 'ies'} restored` : 'All standard categories are already present');
      render();
    });
    $('#dSample', view).addEventListener('click', () => loadSampleData());
    $('#dErase', view).addEventListener('click', () => {
      const m = openModal({
        title: 'Erase all data', size: 'sm',
        body: `<p>This permanently deletes <b>${state.transactions.length}</b> transactions, all receipts and all categories. It cannot be undone.</p>
          <label class="field"><span>Type <b>DELETE</b> to confirm</span><input class="input" id="eConfirm" autocomplete="off"></label>`,
        footer: `<button class="btn" type="button" data-close>Cancel</button><button class="btn danger solid" type="button" id="eGo" disabled>Erase everything</button>`,
      });
      $('#eConfirm', m.el).addEventListener('input', (e) => { $('#eGo', m.el).disabled = e.target.value.trim() !== 'DELETE'; });
      $('#eGo', m.el).addEventListener('click', async () => {
        await DB.clearAll();
        state.settings = { ...DEFAULT_SETTINGS };
        await DB.setMeta('settings', state.settings);
        await seedCategories(true);
        await loadAll();
        m.close();
        toast('All data erased');
        go('dashboard');
      });
    });
  }

  /* =========================================================
   * Seeding, sample data, loading
   * ======================================================= */
  async function seedCategories(all) {
    const have = new Set(state.categories.map((c) => `${c.type}|${c.name.toLowerCase()}`));
    const now = new Date().toISOString();
    const add = DEFAULT_CATEGORIES.filter((d) => all || !have.has(`${d.type}|${d.name.toLowerCase()}`))
      .map((d) => ({ id: uid(), name: d.name, type: d.type, icon: d.icon, budget: 0, createdAt: now }));
    if (add.length) {
      await DB.putMany('categories', add);
      state.categories.push(...add);
      indexCategories();
    }
    return add.length;
  }

  async function loadSampleData() {
    if (state.transactions.length) return;
    const ok = await confirmDialog('Add about six months of example transactions? You can remove them later with <b>Settings → Erase all data</b>.', { title: 'Load sample data', okLabel: 'Load sample data' });
    if (!ok) return;
    await seedCategories(false);
    const byName = (n) => state.categories.find((c) => c.name === n);
    let seed = 42;
    const rnd = () => { seed = (seed * 16807) % 2147483647; return (seed - 1) / 2147483646; };
    const between = (a, b) => Math.round((a + rnd() * (b - a)) * 100);
    const pick = (arr) => arr[Math.floor(rnd() * arr.length)];
    const out = [];
    const now = new Date().toISOString();
    const add = (date, catName, amount, description, vendor, paymentMethod) => {
      const c = byName(catName);
      if (!c) return;
      out.push({ id: uid(), type: c.type, categoryId: c.id, amount, date, description, vendor, paymentMethod, notes: '', createdAt: now, updatedAt: now });
    };
    const today = todayISO();
    for (let k = 5; k >= 0; k--) {
      const ym = addMonths(thisMonth(), -k);
      const [y, mo] = ym.split('-').map(Number);
      const dim = new Date(y, mo, 0).getDate();
      const d = (day) => `${ym}-${pad(Math.min(day, dim))}`;
      const ok2 = (date) => date <= today;
      const fixed = [
        [1, 'Rent & Lease', 180000, 'Monthly shop rent', 'Oakview Properties', 'Bank transfer'],
        [3, 'Software & Subscriptions', 4900, 'Accounting software', 'CloudBooks', 'Credit card'],
        [5, 'Telephone & Internet', 8900, 'Business broadband', 'NetLink', 'Bank transfer'],
        [8, 'Insurance', 21500, 'Liability insurance', 'SafeGuard Insurance', 'Bank transfer'],
        [12, 'Utilities', between(140, 260), 'Electricity bill', 'City Power', 'Bank transfer'],
        [15, 'Payroll & Wages', 320000, 'Payroll — 1st half', 'Staff', 'Bank transfer'],
        [28, 'Payroll & Wages', 320000, 'Payroll — 2nd half', 'Staff', 'Bank transfer'],
        [20, 'Advertising & Marketing', between(150, 450), 'Social media ads', 'AdReach', 'Credit card'],
      ];
      fixed.forEach(([day, ...rest]) => { if (ok2(d(day))) add(d(day), ...rest); });
      for (let day = 1; day <= dim; day++) {
        const date = d(day);
        if (!ok2(date)) break;
        const dow = parseISO(date).getDay();
        if (dow !== 0) {
          const n = 1 + Math.floor(rnd() * 3);
          for (let i = 0; i < n; i++) add(date, 'Sales Revenue', between(120, 900), pick(['Retail sales', 'Online order', 'Counter sales', 'Wholesale order']), pick(['Walk-in', 'Web store', 'Market stall', '']), pick(['Cash', 'Debit card', 'Credit card', 'Mobile payment']));
        }
        if (rnd() < 0.18) add(date, 'Service Income', between(250, 1500), pick(['Installation job', 'Repair service', 'Maintenance contract']), pick(['Greenfield Ltd', 'J. Patel', 'Northside Cafe', 'Harbor Dental']), 'Bank transfer');
        if (rnd() < 0.30) add(date, 'Office Supplies', between(8, 85), pick(['Printer paper', 'Ink cartridges', 'Stationery', 'Cleaning supplies']), pick(['OfficeMart', 'Staples Depot', 'Corner Store']), pick(['Cash', 'Debit card']));
        if (rnd() < 0.22) add(date, 'Vehicle & Fuel', between(30, 95), 'Fuel', pick(['Shell', 'Petro Plus', 'GasGo']), 'Debit card');
        if (rnd() < 0.15) add(date, 'Meals & Entertainment', between(15, 120), pick(['Client lunch', 'Team coffee', 'Supplier meeting']), pick(['Bistro 21', 'Bean There', 'Golden Wok']), 'Credit card');
        if (rnd() < 0.12) add(date, 'Cost of Goods / Inventory', between(300, 2200), 'Stock purchase', pick(['Acme Wholesale', 'Prime Distributors']), 'Bank transfer');
        if (rnd() < 0.08) add(date, 'Shipping & Postage', between(12, 70), 'Courier', pick(['FastShip', 'Post Office']), 'Debit card');
        if (rnd() < 0.05) add(date, 'Bank & Merchant Fees', between(5, 40), 'Card processing fees', 'Bank', 'Bank transfer');
        if (rnd() < 0.03) add(date, 'Repairs & Maintenance', between(80, 400), 'Equipment repair', 'FixIt Services', 'Cash');
      }
    }
    await DB.putMany('transactions', out);
    // a couple of budgets so the dashboard shows progress
    for (const [n, b] of [['Office Supplies', 40000], ['Vehicle & Fuel', 50000], ['Meals & Entertainment', 25000], ['Advertising & Marketing', 40000]]) {
      const c = byName(n);
      if (c) { c.budget = b; await DB.put('categories', c); }
    }
    state.transactions.push(...out);
    sortTx();
    toast(`${out.length} sample transactions added`);
    go('dashboard');
  }

  async function loadAll() {
    const saved = await DB.getMeta('settings');
    state.settings = { ...DEFAULT_SETTINGS, ...(saved || {}) };
    state.categories = await DB.getAll('categories');
    state.transactions = await DB.getAll('transactions');
    const rmeta = await DB.receiptIndex();
    state.receiptCounts = new Map();
    for (const r of rmeta) state.receiptCounts.set(r.transactionId, (state.receiptCounts.get(r.transactionId) || 0) + 1);
    indexCategories();
    sortTx();
    applySettings();
  }

  async function init() {
    renderNav();
    $('#menuBtn').addEventListener('click', () => toggleSidebar());
    $('#scrim').addEventListener('click', () => toggleSidebar(false));
    $('#nav').addEventListener('click', (e) => { if (e.target.closest('a')) toggleSidebar(false); });
    $('#quickAdd').addEventListener('click', () => openTransactionForm());
    window.addEventListener('hashchange', () => { render(); window.scrollTo(0, 0); });
    try {
      await DB.open();
      await loadAll();
      const initialized = await DB.getMeta('initialized');
      if (!initialized) {
        if (!state.categories.length) await seedCategories(true);
        await DB.setMeta('initialized', true);
        await DB.setMeta('settings', state.settings);
      }
    } catch (err) {
      console.error(err);
      $('#view').innerHTML = `<div class="card"><div class="empty"><div class="big">⚠️</div><h3>Storage unavailable</h3><p>${esc(err.message || err)}</p><p>Private browsing modes sometimes block local storage. Try a normal window.</p></div></div>`;
      return;
    }
    render();
  }

  // Expose a tiny API for debugging / testing.
  window.Ledgerly = { state, openTransactionForm, render };
  init();
})();
