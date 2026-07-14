#pragma once

// Embedded GardenESP web UI (PROGMEM)
const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>GardenESP</title>
  <style>
    :root {
      --bg: #e8f0e4;
      --bg2: #d4e4cc;
      --ink: #1a2e14;
      --muted: #4a6340;
      --accent: #2f6b3a;
      --accent-dark: #1f4a28;
      --line: #b8c9ae;
      --warn: #8a4b1a;
      --ok: #1f5c32;
      --surface: rgba(255, 255, 255, 0.55);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: "Segoe UI", "Trebuchet MS", sans-serif;
      color: var(--ink);
      background:
        radial-gradient(ellipse 80% 50% at 10% 0%, #c5dbb8 0%, transparent 55%),
        radial-gradient(ellipse 70% 45% at 100% 20%, #b7d0c8 0%, transparent 50%),
        linear-gradient(165deg, var(--bg), var(--bg2));
    }
    main {
      max-width: 30rem;
      margin: 0 auto;
      padding: 1.35rem 1.05rem 5.5rem;
    }
    h1 {
      margin: 0 0 0.2rem;
      font-size: 1.85rem;
      letter-spacing: -0.03em;
      font-weight: 700;
    }
    .tagline {
      margin: 0 0 1rem;
      color: var(--muted);
      font-size: 0.92rem;
    }
    .last-water {
      padding: 0.85rem 0.95rem;
      margin-bottom: 1rem;
      border: 1px solid var(--line);
      border-radius: 0.5rem;
      background: var(--surface);
    }
    .last-water .label {
      display: block;
      font-size: 0.72rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      color: var(--muted);
      margin-bottom: 0.25rem;
    }
    .last-water .value {
      font-size: 1.05rem;
      font-weight: 650;
    }
    .last-water .ago {
      margin-top: 0.2rem;
      font-size: 0.85rem;
      color: var(--muted);
    }
    .status {
      display: grid;
      gap: 0.3rem;
      padding: 0.75rem 0;
      margin-bottom: 1rem;
      border-top: 1px solid var(--line);
      border-bottom: 1px solid var(--line);
      font-size: 0.86rem;
      color: var(--muted);
    }
    .status strong { color: var(--ink); font-weight: 600; }
    .pump {
      padding: 0.95rem 0 1.05rem;
      border-bottom: 1px solid var(--line);
    }
    .pump-top {
      margin-bottom: 0.65rem;
    }
    .pump-row {
      display: flex;
      align-items: flex-end;
      gap: 0.55rem;
    }
    .pump-id {
      flex: 0 0 auto;
      min-width: 2.4rem;
      padding-bottom: 0.55rem;
      font-size: 1.2rem;
      line-height: 1;
      color: var(--ink);
      font-weight: 700;
    }
    .name-wrap {
      flex: 1;
      min-width: 0;
      display: flex;
      flex-direction: column;
      gap: 0.28rem;
    }
    .name-wrap span {
      font-size: 0.72rem;
      text-transform: uppercase;
      letter-spacing: 0.04em;
      color: var(--muted);
    }
    input[type="text"],
    input[type="number"] {
      width: 100%;
      padding: 0.5rem 0.6rem;
      border: 1px solid var(--line);
      border-radius: 0.4rem;
      background: var(--surface);
      color: var(--ink);
      font-size: 1rem;
    }
    input:focus {
      outline: 2px solid color-mix(in srgb, var(--accent) 45%, transparent);
      border-color: var(--accent);
    }
    label.switch {
      display: inline-flex;
      align-items: center;
      gap: 0.4rem;
      font-size: 0.84rem;
      color: var(--muted);
      cursor: pointer;
      user-select: none;
      white-space: nowrap;
    }
    label.switch input { width: 1.05rem; height: 1.05rem; accent-color: var(--accent); }
    .fields {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 0.6rem;
      margin-bottom: 0.7rem;
    }
    .field {
      display: flex;
      flex-direction: column;
      gap: 0.28rem;
    }
    .field span {
      font-size: 0.72rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }
    button {
      appearance: none;
      border: none;
      border-radius: 0.45rem;
      padding: 0.65rem 0.95rem;
      font-size: 0.92rem;
      font-weight: 600;
      cursor: pointer;
    }
    button.primary {
      background: var(--accent);
      color: #f4faf3;
    }
    button.primary:hover { background: var(--accent-dark); }
    button.ghost {
      background: transparent;
      color: var(--accent-dark);
      border: 1px solid var(--line);
    }
    button.ghost:hover { border-color: var(--accent); }
    button:disabled { opacity: 0.55; cursor: wait; }
    .save-bar {
      position: fixed;
      left: 0;
      right: 0;
      bottom: 0;
      padding: 0.75rem 1.05rem calc(0.75rem + env(safe-area-inset-bottom));
      background: color-mix(in srgb, var(--bg2) 88%, transparent);
      backdrop-filter: blur(8px);
      border-top: 1px solid var(--line);
      display: flex;
      gap: 0.55rem;
      align-items: center;
      justify-content: center;
    }
    .save-bar button { min-width: 10rem; }
    #msg {
      min-height: 1.2rem;
      margin: 0.75rem 0 0;
      font-size: 0.88rem;
      color: var(--ok);
    }
    #msg.error { color: var(--warn); }
  </style>
</head>
<body>
  <main>
    <h1>GardenESP</h1>
    <p class="tagline">Управление насосами полива</p>

    <div class="last-water" id="lastWaterBox">
      <span class="label">Последний полив</span>
      <div class="value" id="lastWaterName">ещё не было</div>
      <div class="ago" id="lastWaterAgo"></div>
    </div>

    <div class="status" id="status">
      <div>AP: <strong id="ap">—</strong></div>
      <div>IP: <strong id="ip">—</strong></div>
      <div>Сервер: <strong id="alive">загрузка…</strong></div>
      <div>Полив: <strong id="watering">—</strong></div>
    </div>

    <form id="configForm">
      <div id="pumps"></div>
      <p id="msg"></p>
      <div class="save-bar">
        <button type="submit" class="primary" id="saveBtn">Сохранить</button>
      </div>
    </form>
  </main>

  <script>
    const pumpsEl = document.getElementById('pumps');
    const msgEl = document.getElementById('msg');
    const form = document.getElementById('configForm');
    let pumps = [];
    let lastWatered = null;
    let watering = { active: false, pump: null, remainSec: 0 };

    function escapeHtml(s) {
      return String(s)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
    }

    function setMsg(text, isError) {
      msgEl.textContent = text || '';
      msgEl.className = isError ? 'error' : '';
    }

    function formatAgo(sec) {
      if (sec == null || sec < 0) return '';
      if (sec < 60) return sec + ' с назад';
      const min = Math.floor(sec / 60);
      if (min < 60) return min + ' мин назад';
      const hours = Math.floor(min / 60);
      if (hours < 48) return hours + ' ч назад';
      const days = Math.floor(hours / 24);
      return days + ' дн назад';
    }

    function renderLastWatered() {
      const nameEl = document.getElementById('lastWaterName');
      const agoEl = document.getElementById('lastWaterAgo');
      if (!lastWatered || lastWatered.index == null || lastWatered.index < 0) {
        nameEl.textContent = 'ещё не было';
        agoEl.textContent = '';
        return;
      }
      const n = lastWatered.name || ('Насос ' + (lastWatered.index + 1));
      nameEl.textContent = (lastWatered.index + 1) + '. ' + n;
      agoEl.textContent = formatAgo(lastWatered.agoSec);
    }

    function renderWatering() {
      const el = document.getElementById('watering');
      if (!watering || !watering.active) {
        el.textContent = 'нет';
        return;
      }
      const idx = Number(watering.pump);
      const name = (pumps[idx] && pumps[idx].name)
        ? pumps[idx].name
        : ('Насос ' + (idx + 1));
      const remain = Number(watering.remainSec) || 0;
      el.textContent = (idx + 1) + '. ' + name + ' · ещё ' + remain + ' с';
    }

    function renderPumps() {
      pumpsEl.innerHTML = pumps.map((p, i) => `
        <section class="pump" data-i="${i}">
          <div class="pump-top">
            <div class="pump-row">
              <div class="pump-id">№${i + 1}</div>
              <label class="name-wrap">
                <span>Имя насоса</span>
                <input type="text" maxlength="23" data-field="name" value="${escapeHtml(p.name || ('Насос ' + (i + 1)))}">
              </label>
              <label class="switch">
                <input type="checkbox" data-field="enabled" ${p.enabled ? 'checked' : ''}>
                Вкл
              </label>
            </div>
          </div>
          <div class="fields">
            <label class="field">
              <span>Длительность, сек</span>
              <input type="number" min="1" max="3600" data-field="durationSec" value="${Number(p.durationSec) || 30}">
            </label>
            <label class="field">
              <span>Интервал, часы</span>
              <input type="number" min="1" max="8760" data-field="intervalHours" value="${Number(p.intervalHours) || 48}">
            </label>
          </div>
          <button type="button" class="ghost" data-water="${i}" ${watering && watering.active ? 'disabled' : ''}>Полить сейчас</button>
        </section>
      `).join('');

      pumpsEl.querySelectorAll('[data-water]').forEach(btn => {
        btn.addEventListener('click', () => waterNow(Number(btn.dataset.water)));
      });
    }

    function readPumpsFromDom() {
      return Array.from(pumpsEl.querySelectorAll('.pump')).map((section, i) => {
        const name = (section.querySelector('[data-field="name"]').value || '').trim()
          || ('Насос ' + (i + 1));
        const enabled = section.querySelector('[data-field="enabled"]').checked;
        const durationSec = Number(section.querySelector('[data-field="durationSec"]').value) || 1;
        const intervalHours = Number(section.querySelector('[data-field="intervalHours"]').value) || 1;
        return { name, enabled, durationSec, intervalHours };
      });
    }

    function applyStatus(data, forcePumps) {
      document.getElementById('ap').textContent = data.apSsid || '—';
      document.getElementById('ip').textContent = data.ip || '—';
      document.getElementById('alive').textContent = 'онлайн · uptime ' + (data.uptimeSec || 0) + ' с';
      const nextPumps = Array.isArray(data.pumps) ? data.pumps : [];
      const needRenderPumps = forcePumps
        || pumps.length !== nextPumps.length
        || pumpsEl.children.length !== nextPumps.length;
      pumps = nextPumps;
      lastWatered = data.lastWatered || null;
      watering = data.watering || { active: false, pump: null, remainSec: 0 };
      renderLastWatered();
      renderWatering();
      if (needRenderPumps) {
        renderPumps();
      } else {
        pumpsEl.querySelectorAll('[data-water]').forEach(btn => {
          btn.disabled = !!(watering && watering.active);
        });
      }
    }

    async function loadStatus() {
      try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        applyStatus(await res.json());
        setMsg('');
      } catch (e) {
        document.getElementById('alive').textContent = 'нет связи';
        setMsg('Не удалось загрузить статус', true);
      }
    }

    async function waterNow(index) {
      setMsg('Запрос полива…');
      try {
        const body = new URLSearchParams({ pump: String(index) });
        const res = await fetch('/api/water', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body
        });
        const data = await res.json();
        if (data.watering) watering = data.watering;
        if (data.lastWatered) lastWatered = data.lastWatered;
        renderLastWatered();
        renderWatering();
        renderPumps();
        if (res.status === 409 || data.message === 'busy') {
          const remain = watering && watering.remainSec != null ? watering.remainSec : 0;
          setMsg('Занято: уже идёт полив' + (remain ? (' · ещё ' + remain + ' с') : ''), true);
          return;
        }
        if (!res.ok || !data.ok) throw new Error(data.message || 'Ошибка');
        setMsg(data.message || 'Полив запрошен');
      } catch (e) {
        setMsg(e.message || 'Ошибка полива', true);
      }
    }

    form.addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const saveBtn = document.getElementById('saveBtn');
      saveBtn.disabled = true;
      setMsg('Сохранение…');
      try {
        const payload = { pumps: readPumpsFromDom() };
        const res = await fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const data = await res.json();
        if (!res.ok || !data.ok) throw new Error(data.message || 'Ошибка сохранения');
        if (data.pumps) pumps = data.pumps;
        if (data.lastWatered !== undefined) lastWatered = data.lastWatered;
        renderLastWatered();
        renderPumps();
        setMsg(data.message || 'Сохранено');
      } catch (e) {
        setMsg(e.message || 'Не удалось сохранить', true);
      } finally {
        saveBtn.disabled = false;
      }
    });

    loadStatus();
    setInterval(loadStatus, 2000);
  </script>
</body>
</html>
)rawliteral";
