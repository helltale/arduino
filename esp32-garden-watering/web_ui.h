#pragma once

const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport"
      content="width=device-width, initial-scale=1">

<title>GardenESP</title>

<style>
:root {
  --bg: #eef3eb;
  --card: #ffffff;
  --green: #347c51;
  --green-dark: #245d3c;
  --text: #24352a;
  --muted: #748277;
  --border: #dce7dc;
  --danger: #c94f49;
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: system-ui, Arial, sans-serif;
  background: var(--bg);
  color: var(--text);
}

main {
  max-width: 850px;
  margin: auto;
  padding: 22px 15px 110px;
}

h1 {
  margin: 0;
  color: var(--green-dark);
  font-size: 2rem;
}

.subtitle {
  margin: 6px 0 22px;
  color: var(--muted);
}

.card {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: 15px;
  padding: 17px;
  margin-bottom: 14px;
  box-shadow: 0 3px 12px #203b2010;
}

.label {
  color: var(--muted);
  font-size: 0.78rem;
}

.value {
  font-size: 1.12rem;
  font-weight: 650;
  margin-top: 5px;
}

.status-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 14px;
}

.topline {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}

.pump-number {
  font-weight: 750;
  color: var(--green-dark);
}

.pump-name {
  width: 100%;
  margin-bottom: 12px;
}

input[type="text"],
input[type="number"] {
  width: 100%;
  padding: 11px;
  border: 1px solid var(--border);
  border-radius: 9px;
  font-size: 0.95rem;
  background: #fff;
  color: var(--text);
}

input:focus {
  outline: 2px solid #78bd89;
}

.fields {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.field-label {
  display: block;
  font-size: 0.78rem;
  color: var(--muted);
  margin: 0 0 5px;
}

.switch {
  display: flex;
  gap: 7px;
  align-items: center;
  font-size: 0.85rem;
}

.switch input {
  accent-color: var(--green);
  width: 18px;
  height: 18px;
}

.due {
  color: var(--muted);
  font-size: 0.86rem;
  margin-top: 12px;
  margin-bottom: 13px;
}

button {
  cursor: pointer;
  border-radius: 9px;
  padding: 11px 16px;
  font-size: 0.94rem;
  font-weight: 650;
  border: 1px solid var(--border);
}

button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.primary {
  background: var(--green);
  color: white;
  border-color: var(--green);
}

.secondary {
  color: var(--green-dark);
  background: #f1f8f0;
}

.danger {
  background: var(--danger);
  border-color: var(--danger);
  color: white;
}

.buttons {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.save-bar {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  background: #fffffff2;
  border-top: 1px solid var(--border);
  padding: 13px 16px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 16px;
}

#message {
  font-size: 0.86rem;
  color: var(--green-dark);
  max-width: 350px;
}

#message.error {
  color: var(--danger);
}

.active {
  border-color: #6cab7d;
  background: #f4fff4;
}

@media (max-width: 520px) {
  main {
    padding: 18px 12px 115px;
  }

  .status-grid {
    grid-template-columns: 1fr 1fr;
  }

  .fields {
    grid-template-columns: 1fr 1fr;
  }

  .save-bar {
    justify-content: space-between;
  }
}
</style>
</head>

<body>
<main>

  <h1>GardenESP</h1>
  <p class="subtitle">
    Умная система полива · 16 насосов
  </p>

  <div class="card">
    <span class="label">Последний полив</span>
    <div class="value" id="lastWater">
      Ещё не было
    </div>
  </div>

  <div class="card">
    <div class="status-grid">
      <div>
        <span class="label">Wi-Fi</span>
        <div class="value" id="ap">—</div>
      </div>
      <div>
        <span class="label">IP-адрес</span>
        <div class="value" id="ip">—</div>
      </div>
      <div>
        <span class="label">Контроллер</span>
        <div class="value" id="online">—</div>
      </div>
      <div>
        <span class="label">Состояние</span>
        <div class="value" id="watering">—</div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="topline">
      <strong>Управление поливом</strong>
    </div>

    <div class="buttons">
      <button type="button"
              class="danger"
              id="stopBtn"
              onclick="stopWatering()">
        Остановить полив
      </button>
    </div>

    <p class="due">
      Одновременно может работать только один насос.
    </p>
  </div>

  <form id="configForm">
    <div id="pumpList"></div>
  </form>

</main>

<div class="save-bar">
  <span id="message"></span>
  <button type="submit"
          class="primary"
          form="configForm"
          id="saveBtn">
    Сохранить
  </button>
</div>

<script>
let pumps = [];
let watering = {
  active: false,
  pump: null,
  remainSec: 0
};

let initialized = false;
let busyRequest = false;

const list = document.getElementById('pumpList');
const message = document.getElementById('message');

function showMessage(text, error = false) {
  message.textContent = text;
  message.className = error ? 'error' : '';
}

function escapeHtml(text) {
  return String(text)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function formatTime(seconds) {
  seconds = Math.max(0, Math.floor(Number(seconds) || 0));

  if (seconds < 60) {
    return seconds + ' сек';
  }

  let minutes = Math.floor(seconds / 60);

  if (minutes < 60) {
    return minutes + ' мин';
  }

  let hours = Math.floor(minutes / 60);

  if (hours < 24) {
    return hours + ' ч ' + (minutes % 60) + ' мин';
  }

  let days = Math.floor(hours / 24);

  return days + ' дн ' + (hours % 24) + ' ч';
}

function renderPumps() {
  list.innerHTML = pumps.map((pump, index) => `
    <section class="card pump"
             data-index="${index}"
             id="pump-${index}">

      <div class="topline">
        <span class="pump-number">
          Насос №${index + 1}
        </span>

        <label class="switch">
          <input type="checkbox"
                 data-field="enabled"
                 ${pump.enabled ? 'checked' : ''}>
          Автополив
        </label>
      </div>

      <label class="field-label">
        Название
      </label>

      <input type="text"
             class="pump-name"
             data-field="name"
             maxlength="48"
             value="${escapeHtml(pump.name)}">

      <div class="fields">
        <div>
          <label class="field-label">
            Длительность, сек
          </label>

          <input type="number"
                 min="1"
                 max="3600"
                 data-field="durationSec"
                 value="${pump.durationSec}">
        </div>

        <div>
          <label class="field-label">
            Интервал, часы
          </label>

          <input type="number"
                 min="1"
                 max="8760"
                 data-field="intervalHours"
                 value="${pump.intervalHours}">
        </div>
      </div>

      <div class="due" data-due>
        ${pump.enabled
          ? 'Следующий полив через: ' +
            formatTime(pump.dueInSec)
          : 'Автополив выключен'}
      </div>

      <button type="button"
              class="secondary"
              data-start="${index}"
              onclick="waterNow(${index})">
        Полить сейчас
      </button>

    </section>
  `).join('');

  updateControls();
}

function updateControls() {
  list.querySelectorAll('[data-start]').forEach(button => {
    button.disabled = busyRequest || watering.active;
  });

  document.getElementById('stopBtn').disabled =
      busyRequest || !watering.active;

  list.querySelectorAll('.pump').forEach(section => {
    let index = Number(section.dataset.index);

    section.classList.toggle(
      'active',
      watering.active && watering.pump === index
    );

    const due = section.querySelector('[data-due]');

    if (pumps[index]) {
      due.textContent = pumps[index].enabled
        ? 'Следующий полив через: ' +
          formatTime(pumps[index].dueInSec)
        : 'Автополив выключен';
    }
  });
}

function updateStatus(data) {
  document.getElementById('ap').textContent =
      data.apSsid || '—';

  document.getElementById('ip').textContent =
      data.ip || '—';

  document.getElementById('online').textContent =
      'Онлайн';

  watering = data.watering || {
    active: false,
    pump: null,
    remainSec: 0
  };

  let wateringEl =
      document.getElementById('watering');

  if (watering.active) {
    wateringEl.textContent =
        'Насос №' + (watering.pump + 1) +
        ' · ' + formatTime(watering.remainSec);
  } else {
    wateringEl.textContent = 'Ожидание';
  }

  let last = data.lastWatered;

  document.getElementById('lastWater').textContent =
      last
      ? `${last.index + 1}. ${last.name} · ${formatTime(last.agoSec)} назад`
      : 'Ещё не было';

  const incoming = Array.isArray(data.pumps)
      ? data.pumps : [];

  if (!initialized) {
    pumps = incoming;
    renderPumps();
    initialized = true;
  } else {
    // Обновляем данные о расписании,
    // не стирая вводимые пользователем значения.
    incoming.forEach((pump, index) => {
      if (pumps[index]) {
        pumps[index].dueInSec = pump.dueInSec;
      }
    });
  }

  updateControls();
}

async function loadStatus() {
  try {
    const response = await fetch('/api/status', {
      cache: 'no-store'
    });

    if (!response.ok) {
      throw new Error('Нет связи с ESP32');
    }

    const data = await response.json();

    updateStatus(data);
  } catch (error) {
    document.getElementById('online').textContent =
        'Нет связи';
  }
}

async function waterNow(index) {
  if (busyRequest || watering.active) return;

  busyRequest = true;
  updateControls();

  try {
    let body = new URLSearchParams({
      pump: String(index)
    });

    let response = await fetch('/api/water', {
      method: 'POST',
      body
    });

    let data = await response.json();

    if (!response.ok || !data.ok) {
      throw new Error(
        data.message === 'busy'
          ? 'Уже работает другой насос'
          : (data.message || 'Ошибка запуска')
      );
    }

    showMessage('Насос №' + (index + 1) + ' запущен');
    updateStatus(data);
  } catch (error) {
    showMessage(error.message, true);
  } finally {
    busyRequest = false;
    updateControls();
  }
}

async function stopWatering() {
  if (busyRequest) return;

  busyRequest = true;
  updateControls();

  try {
    let response = await fetch('/api/stop', {
      method: 'POST'
    });

    let data = await response.json();

    if (!response.ok || !data.ok) {
      throw new Error('Не удалось остановить полив');
    }

    showMessage('Полив остановлен');
    updateStatus(data);
  } catch (error) {
    showMessage(error.message, true);
  } finally {
    busyRequest = false;
    updateControls();
  }
}

function readConfig() {
  return Array.from(
    list.querySelectorAll('.pump')
  ).map((section, index) => {

    let name = section
      .querySelector('[data-field="name"]')
      .value.trim();

    let enabled = section
      .querySelector('[data-field="enabled"]')
      .checked;

    let durationSec = Number(section
      .querySelector('[data-field="durationSec"]')
      .value);

    let intervalHours = Number(section
      .querySelector('[data-field="intervalHours"]')
      .value);

    if (!Number.isInteger(durationSec) ||
        durationSec < 1 || durationSec > 3600) {
      throw new Error(
        'Неверная длительность насоса №' + (index + 1)
      );
    }

    if (!Number.isInteger(intervalHours) ||
        intervalHours < 1 || intervalHours > 8760) {
      throw new Error(
        'Неверный интервал насоса №' + (index + 1)
      );
    }

    return {
      name: name || ('Насос ' + (index + 1)),
      enabled,
      durationSec,
      intervalHours
    };
  });
}

document.getElementById('configForm')
  .addEventListener('submit', async event => {

    event.preventDefault();

    if (busyRequest) return;

    busyRequest = true;
    updateControls();

    try {
      let config = readConfig();

      let response = await fetch('/api/config', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({ pumps: config })
      });

      let data = await response.json();

      if (!response.ok || !data.ok) {
        throw new Error(
          data.message || 'Ошибка сохранения'
        );
      }

      pumps = data.pumps;
      renderPumps();
      updateStatus(data);

      showMessage('Настройки сохранены');
    } catch (error) {
      showMessage(error.message, true);
    } finally {
      busyRequest = false;
      updateControls();
    }
  });

loadStatus();

// Получаем состояние каждые 2 секунды
setInterval(loadStatus, 2000);
</script>

</body>
</html>
)rawliteral";
