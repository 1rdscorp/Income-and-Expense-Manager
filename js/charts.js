/* Lightweight SVG charts (no dependencies).
 * Grouped vertical bars with a hover tooltip per column. */
const Charts = (() => {
  const SVG_NS = 'http://www.w3.org/2000/svg';

  function niceScale(max, ticks = 4) {
    if (!(max > 0)) return { max: 10000, step: 2500 };
    const raw = max / ticks;
    const mag = Math.pow(10, Math.floor(Math.log10(raw)));
    const norm = raw / mag;
    const step = (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5 ? 2.5 : norm <= 5 ? 5 : 10) * mag;
    return { max: Math.ceil(max / step) * step, step };
  }

  /* Rounded top corners, square at the baseline. */
  function barPath(x, y, w, h, r) {
    r = Math.min(r, w / 2, h);
    return `M${x},${y + h}V${y + r}Q${x},${y} ${x + r},${y}H${x + w - r}Q${x + w},${y} ${x + w},${y + r}V${y + h}Z`;
  }

  function el(name, attrs = {}) {
    const n = document.createElementNS(SVG_NS, name);
    for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
    return n;
  }

  const escText = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  /**
   * opts: {
   *   labels: string[], fullLabels?: string[],
   *   series: [{ name, colorVar, values: number[] }],
   *   format(v), compact(v), height?, onSelect?(index), footer?(index) -> html
   * }
   */
  function groupedBars(container, opts) {
    container.classList.add('chart');
    let ro;
    const draw = () => {
      const width = Math.max(280, container.clientWidth);
      const height = opts.height || 260;
      const m = { top: 12, right: 8, bottom: 28, left: 64 };
      const iw = width - m.left - m.right;
      const ih = height - m.top - m.bottom;
      const n = opts.labels.length;
      const all = opts.series.flatMap((s) => s.values);
      const { max, step } = niceScale(Math.max(0, ...all));
      const yPos = (v) => m.top + ih - (v / max) * ih;

      container.innerHTML = '';
      const svg = el('svg', { width, height, viewBox: `0 0 ${width} ${height}`, role: 'img', 'aria-label': opts.ariaLabel || 'Bar chart' });

      // Gridlines + y labels
      for (let v = 0; v <= max + 1e-9; v += step) {
        const y = Math.round(yPos(v)) + 0.5;
        svg.appendChild(el('line', { x1: m.left, x2: width - m.right, y1: y, y2: y, class: v === 0 ? 'axis' : 'grid' }));
        const t = el('text', { x: m.left - 8, y: y + 4, 'text-anchor': 'end', class: 'tick' });
        t.textContent = opts.compact(v);
        svg.appendChild(t);
      }

      const band = iw / n;
      const groupW = Math.min(band * 0.72, 64);
      const gap = 2;
      const k = opts.series.length;
      const barW = Math.max(3, (groupW - gap * (k - 1)) / k);
      const hl = el('rect', { class: 'hover-band', y: m.top, height: ih, width: band, x: 0, opacity: 0 });
      svg.appendChild(hl);

      const showEvery = band < 26 ? Math.ceil(26 / band) : 1;
      opts.labels.forEach((label, i) => {
        const gx = m.left + band * i + (band - (barW * k + gap * (k - 1))) / 2;
        opts.series.forEach((s, si) => {
          const v = s.values[i] || 0;
          if (v <= 0) return;
          const y = yPos(v);
          svg.appendChild(el('path', {
            d: barPath(gx + si * (barW + gap), y, barW, m.top + ih - y, 4),
            fill: `var(${s.colorVar})`,
            class: 'bar',
          }));
        });
        if (i % showEvery === 0) {
          const t = el('text', { x: m.left + band * i + band / 2, y: height - 8, 'text-anchor': 'middle', class: 'tick' });
          t.textContent = label;
          svg.appendChild(t);
        }
      });

      // Hit targets (whole column), tooltip
      const tip = document.createElement('div');
      tip.className = 'chart-tip';
      tip.hidden = true;
      opts.labels.forEach((label, i) => {
        const hit = el('rect', { x: m.left + band * i, y: m.top, width: band, height: ih, fill: 'transparent', class: 'hit' });
        if (opts.onSelect) hit.style.cursor = 'pointer';
        const show = () => {
          hl.setAttribute('x', m.left + band * i);
          hl.setAttribute('opacity', 1);
          const title = (opts.fullLabels || opts.labels)[i];
          tip.innerHTML = `<div class="tip-title">${escText(title)}</div>` +
            opts.series.map((s) => `<div class="tip-row"><span class="swatch" style="background:var(${s.colorVar})"></span><span>${escText(s.name)}</span><b>${escText(opts.format(s.values[i] || 0))}</b></div>`).join('') +
            (opts.footer ? opts.footer(i) : '');
          tip.hidden = false;
          const cx = m.left + band * i + band / 2;
          const tw = tip.offsetWidth;
          let left = cx + 12;
          if (left + tw > width) left = cx - tw - 12;
          tip.style.left = `${Math.max(0, left)}px`;
          tip.style.top = `${m.top}px`;
        };
        const hide = () => { tip.hidden = true; hl.setAttribute('opacity', 0); };
        hit.addEventListener('mouseenter', show);
        hit.addEventListener('mouseleave', hide);
        hit.addEventListener('focus', show);
        hit.addEventListener('blur', hide);
        if (opts.onSelect) hit.addEventListener('click', () => opts.onSelect(i));
        svg.appendChild(hit);
      });

      container.appendChild(svg);
      container.appendChild(tip);
    };

    draw();
    if ('ResizeObserver' in window) {
      let last = container.clientWidth;
      ro = new ResizeObserver(() => {
        const w = container.clientWidth;
        if (Math.abs(w - last) > 4) { last = w; draw(); }
      });
      ro.observe(container);
    }
    return () => ro && ro.disconnect();
  }

  return { groupedBars, niceScale };
})();
