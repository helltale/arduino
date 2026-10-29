#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_timer.h>

#include "web_ui.h"

// -------------------- Wi-Fi --------------------

const char* AP_SSID = "esp32-garden-1";
const char* AP_PASS = "begemotic1";

IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_MASK(255, 255, 255, 0);

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// -------------------- Реле --------------------

constexpr uint8_t PUMP_COUNT = 16;

// Порядок: IN1, IN2, ... IN16
const uint8_t RELAY_PINS[PUMP_COUNT] = {
  26, 23, 27, 22,
  32, 21, 33, 19,
  14, 18, 13, 17,
   4, 16,  5, 25
};

// Проверенная логика Active LOW
constexpr uint8_t RELAY_ON  = LOW;
constexpr uint8_t RELAY_OFF = HIGH;

// -------------------- Настройки --------------------

struct PumpConfig {
  String name;
  bool enabled = false;
  uint16_t durationSec = 30;
  uint16_t intervalHours = 48;

  // Время следующего автополива
  uint64_t nextDueSec = 0;
};

PumpConfig pumps[PUMP_COUNT];

// Номер работающего насоса.
// -1 означает, что ничего не работает.
int activePump = -1;

uint32_t wateringStartedMs = 0;
uint32_t wateringDurationMs = 0;

int lastWateredIndex = -1;
uint64_t lastWateredAtSec = 0;

// -------------------- Время --------------------

// Монотонное время работы ESP32.
// Не зависит от переполнения millis().
uint64_t uptimeSec() {
  return (uint64_t)(esp_timer_get_time() / 1000000ULL);
}

uint64_t intervalSec(uint8_t index) {
  return (uint64_t)pumps[index].intervalHours * 3600ULL;
}

void scheduleNext(uint8_t index) {
  pumps[index].nextDueSec =
      uptimeSec() + intervalSec(index);
}

// -------------------- Управление реле --------------------

void relayOff(uint8_t index) {
  if (index >= PUMP_COUNT) return;

  digitalWrite(RELAY_PINS[index], RELAY_OFF);
}

void relayOn(uint8_t index) {
  if (index >= PUMP_COUNT) return;

  digitalWrite(RELAY_PINS[index], RELAY_ON);
}

void allRelaysOff() {
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    relayOff(i);
  }
}

// Вызываем в самом начале setup()
void initRelays() {
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    // Сначала задаём HIGH, потом OUTPUT.
    // Точно как в нашем работающем тесте.
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
    pinMode(RELAY_PINS[i], OUTPUT);
  }

  allRelaysOff();
  activePump = -1;
}

// -------------------- Конфигурация --------------------

void defaultConfig() {
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    pumps[i].name = "Насос " + String(i + 1);
    pumps[i].enabled = false;
    pumps[i].durationSec = 30;
    pumps[i].intervalHours = 48;
    scheduleNext(i);
  }
}

void saveConfig() {
  JsonDocument doc;
  JsonArray arr = doc["pumps"].to<JsonArray>();

  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    JsonObject p = arr.add<JsonObject>();

    p["name"] = pumps[i].name;
    p["enabled"] = pumps[i].enabled;
    p["durationSec"] = pumps[i].durationSec;
    p["intervalHours"] = pumps[i].intervalHours;
  }

  String json;
  serializeJson(doc, json);

  preferences.putString("config", json);
}

void loadConfig() {
  defaultConfig();

  String json = preferences.getString("config", "");

  if (json.isEmpty()) {
    Serial.println("[config] Default configuration");
    return;
  }

  JsonDocument doc;

  if (deserializeJson(doc, json)) {
    Serial.println("[config] Invalid saved JSON");
    return;
  }

  JsonArrayConst arr =
      doc["pumps"].as<JsonArrayConst>();

  if (arr.size() != PUMP_COUNT) {
    Serial.println("[config] Unexpected pump count");
    return;
  }

  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    JsonObjectConst p = arr[i];

    String name = p["name"] | "";

    name.trim();
    if (!name.isEmpty()) {
      pumps[i].name = name.substring(0, 48);
    }

    pumps[i].enabled = p["enabled"] | false;

    int duration = p["durationSec"] | 30;
    int interval = p["intervalHours"] | 48;

    pumps[i].durationSec =
        constrain(duration, 1, 3600);

    pumps[i].intervalHours =
        constrain(interval, 1, 8760);

    // После перезагрузки расписание начинается
    // заново: часов реального времени у нас нет.
    scheduleNext(i);
  }

  Serial.println("[config] Loaded from flash");
}

// -------------------- Запуск и остановка --------------------

bool startWatering(uint8_t index) {
  if (index >= PUMP_COUNT) return false;
  if (activePump >= 0) return false;

  // Гарантированно выключаем остальные
  allRelaysOff();

  activePump = index;
  wateringStartedMs = millis();

  wateringDurationMs =
      (uint32_t)pumps[index].durationSec * 1000UL;

  lastWateredIndex = index;
  lastWateredAtSec = uptimeSec();

  // Отсчёт следующего цикла от этого запуска
  scheduleNext(index);

  relayOn(index);

  Serial.printf(
    "[water] ON pump %u, duration %u sec\n",
    index + 1,
    pumps[index].durationSec
  );

  return true;
}

void stopWatering() {
  if (activePump < 0) {
    allRelaysOff();
    return;
  }

  int finishedPump = activePump;

  // Сначала отключаем физическое реле
  relayOff((uint8_t)finishedPump);
  allRelaysOff();

  activePump = -1;
  wateringDurationMs = 0;

  Serial.printf(
    "[water] OFF pump %d\n",
    finishedPump + 1
  );
}

uint32_t remainingSec() {
  if (activePump < 0) return 0;

  uint32_t elapsed = millis() - wateringStartedMs;

  if (elapsed >= wateringDurationMs) return 0;

  return (wateringDurationMs - elapsed + 999UL)
      / 1000UL;
}

void updateWatering() {
  // Выключение по таймеру
  if (activePump >= 0) {
    if ((uint32_t)(millis() - wateringStartedMs)
        >= wateringDurationMs) {
      stopWatering();
    }
  }

  // Пока один насос работает,
  // второй не запускаем.
  if (activePump >= 0) return;

  uint64_t now = uptimeSec();

  // Проверяем ожидающие автополивы.
  // Если несколько готовы, запускаем
  // только первый по номеру.
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    if (!pumps[i].enabled) continue;

    if (now >= pumps[i].nextDueSec) {
      startWatering(i);
      break;
    }
  }
}

// -------------------- Формирование JSON --------------------

void appendStatus(JsonDocument& doc) {
  doc["apSsid"] = AP_SSID;
  doc["ip"] = WiFi.softAPIP().toString();
  doc["uptimeSec"] = uptimeSec();
  doc["pumpCount"] = PUMP_COUNT;

  JsonObject watering =
      doc["watering"].to<JsonObject>();

  watering["active"] = activePump >= 0;

  if (activePump >= 0) {
    watering["pump"] = activePump;
    watering["remainSec"] = remainingSec();
  } else {
    watering["pump"] = nullptr;
    watering["remainSec"] = 0;
  }

  if (lastWateredIndex >= 0) {
    JsonObject last =
        doc["lastWatered"].to<JsonObject>();

    last["index"] = lastWateredIndex;
    last["name"] = pumps[lastWateredIndex].name;
    last["agoSec"] =
        uptimeSec() - lastWateredAtSec;
  } else {
    doc["lastWatered"] = nullptr;
  }

  JsonArray arr = doc["pumps"].to<JsonArray>();
  uint64_t now = uptimeSec();

  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    JsonObject p = arr.add<JsonObject>();

    p["name"] = pumps[i].name;
    p["enabled"] = pumps[i].enabled;
    p["durationSec"] = pumps[i].durationSec;
    p["intervalHours"] = pumps[i].intervalHours;

    uint32_t due = 0;

    if (pumps[i].nextDueSec > now) {
      due = (uint32_t)(pumps[i].nextDueSec - now);
    }

    p["dueInSec"] = due;
  }
}

void sendJson(int statusCode, JsonDocument& doc) {
  String json;
  serializeJson(doc, json);

  server.send(
    statusCode,
    "application/json; charset=utf-8",
    json
  );
}

void sendError(int statusCode, const char* message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["message"] = message;

  sendJson(statusCode, doc);
}

// -------------------- HTTP API --------------------

void handleRoot() {
  server.send_P(
    200,
    "text/html; charset=utf-8",
    WEB_UI_HTML
  );
}


void handleCaptivePortal() {
  // Отправляем телефон на главную страницу GardenESP
  server.sendHeader(
    "Location",
    "http://192.168.4.1/",
    true
  );

  server.send(
    302,
    "text/plain",
    "Redirecting to GardenESP"
  );
}

void handleStatus() {
  JsonDocument doc;
  appendStatus(doc);

  sendJson(200, doc);
}

void handleConfig() {
  JsonDocument request;

  if (deserializeJson(request, server.arg("plain"))) {
    sendError(400, "Invalid JSON");
    return;
  }

  JsonArrayConst arr =
      request["pumps"].as<JsonArrayConst>();

  if (arr.size() != PUMP_COUNT) {
    sendError(400, "Expected exactly 16 pumps");
    return;
  }

  // Проверяем данные до изменения конфигурации
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    JsonObjectConst p = arr[i];

    if (!p["name"].is<const char*>() ||
        !p["enabled"].is<bool>() ||
        !p["durationSec"].is<int>() ||
        !p["intervalHours"].is<int>()) {
      sendError(400, "Invalid pump settings");
      return;
    }

    int duration = p["durationSec"].as<int>();
    int interval = p["intervalHours"].as<int>();

    if (duration < 1 || duration > 3600 ||
        interval < 1 || interval > 8760) {
      sendError(400, "Value out of range");
      return;
    }
  }

  // Применяем настройки
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    JsonObjectConst p = arr[i];

    String name = p["name"].as<String>();
    name.trim();

    pumps[i].name = name.isEmpty()
        ? "Насос " + String(i + 1)
        : name.substring(0, 48);

    pumps[i].enabled = p["enabled"].as<bool>();
    pumps[i].durationSec =
        p["durationSec"].as<uint16_t>();
    pumps[i].intervalHours =
        p["intervalHours"].as<uint16_t>();

    scheduleNext(i);
  }

  saveConfig();

  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Настройки сохранены";
  appendStatus(response);

  sendJson(200, response);

  Serial.println("[config] Saved");
}

void handleWater() {
  if (!server.hasArg("pump")) {
    sendError(400, "Missing pump");
    return;
  }

  String value = server.arg("pump");
  value.trim();

  for (size_t i = 0; i < value.length(); i++) {
    if (!isDigit(value[i])) {
      sendError(400, "Invalid pump");
      return;
    }
  }

  if (value.isEmpty()) {
    sendError(400, "Invalid pump");
    return;
  }

  int index = value.toInt();

  if (index < 0 || index >= PUMP_COUNT) {
    sendError(400, "Invalid pump");
    return;
  }

  if (activePump >= 0) {
    JsonDocument response;
    response["ok"] = false;
    response["message"] = "busy";
    appendStatus(response);

    sendJson(409, response);
    return;
  }

  if (!startWatering((uint8_t)index)) {
    sendError(409, "Cannot start pump");
    return;
  }

  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Полив запущен";
  appendStatus(response);

  sendJson(200, response);
}

void handleStop() {
  stopWatering();

  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Полив остановлен";
  appendStatus(response);

  sendJson(200, response);
}

// -------------------- Setup --------------------

void setup() {
  // ==========================================
  // 1. Инициализация реле
  // ==========================================

  // Все 16 реле переводим в выключенное состояние
  // согласно проверенной логике Active LOW.
  initRelays();

  // ==========================================
  // 2. Serial Monitor
  // ==========================================

  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("==========================");
  Serial.println("GardenESP starting...");
  Serial.println("==========================");

  // ==========================================
  // 3. Загрузка сохранённых настроек
  // ==========================================

  preferences.begin("garden", false);
  loadConfig();

  Serial.println("[config] Settings loaded");

  // ==========================================
  // 4. Запуск Wi-Fi Access Point
  // ==========================================

  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);

  WiFi.mode(WIFI_AP);

  // Фиксированный IP ESP32
  WiFi.softAPConfig(
    AP_IP,
    AP_IP,
    AP_MASK
  );

  bool apStarted = WiFi.softAP(
    AP_SSID,
    AP_PASS
  );

  // ==========================================
  // 5. Captive Portal DNS
  // ==========================================

  // Перенаправление DNS-запросов на ESP32
  dnsServer.start(53, "*", AP_IP);

  // ==========================================
  // 6. Главная веб-страница
  // ==========================================

  server.on("/", HTTP_GET, handleRoot);

  // ==========================================
  // 7. API управления поливом
  // ==========================================

  server.on(
    "/api/status",
    HTTP_GET,
    handleStatus
  );

  server.on(
    "/api/config",
    HTTP_POST,
    handleConfig
  );

  server.on(
    "/api/water",
    HTTP_POST,
    handleWater
  );

  server.on(
    "/api/stop",
    HTTP_POST,
    handleStop
  );

  // ==========================================
  // 8. Captive Portal для Android
  // ==========================================

  server.on(
    "/generate_204",
    HTTP_GET,
    handleCaptivePortal
  );

  server.on(
    "/gen_204",
    HTTP_GET,
    handleCaptivePortal
  );

  // ==========================================
  // 9. Captive Portal для Apple
  // ==========================================

  server.on(
    "/hotspot-detect.html",
    HTTP_GET,
    handleCaptivePortal
  );

  // ==========================================
  // 10. Captive Portal для Windows
  // ==========================================

  server.on(
    "/connecttest.txt",
    HTTP_GET,
    handleCaptivePortal
  );

  server.on(
    "/ncsi.txt",
    HTTP_GET,
    handleCaptivePortal
  );

  server.on(
    "/fwlink",
    HTTP_GET,
    handleCaptivePortal
  );

  // ==========================================
  // 11. Дополнительные адреса
  // ==========================================

  server.on(
    "/redirect",
    HTTP_GET,
    handleCaptivePortal
  );

  server.on(
    "/canonical.html",
    HTTP_GET,
    handleCaptivePortal
  );

  server.on(
    "/success.txt",
    HTTP_GET,
    handleCaptivePortal
  );

  // Любые неизвестные HTTP-запросы
  // перенаправляем на главную страницу.
  server.onNotFound(handleCaptivePortal);

  // ==========================================
  // 12. Запуск веб-сервера
  // ==========================================

  server.begin();

  // ==========================================
  // 13. Диагностическая информация
  // ==========================================

  Serial.println();
  Serial.println("--------------------------");
  Serial.println("GardenESP ready");

  Serial.print("WiFi SSID: ");
  Serial.println(AP_SSID);

  Serial.print("AP started: ");
  Serial.println(apStarted ? "YES" : "NO");

  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  Serial.println("Captive Portal: enabled");
  Serial.println("--------------------------");

  // ==========================================
  // 14. Финальная проверка реле
  // ==========================================

  allRelaysOff();
}

// -------------------- Loop --------------------

void loop() {
  // Первым делом контролируем время полива
  updateWatering();

  dnsServer.processNextRequest();
  server.handleClient();

  // Повторная проверка после обработки HTTP
  updateWatering();

  delay(1);
}
