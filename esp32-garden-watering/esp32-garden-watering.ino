#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <string.h>
#include "web_ui.h"

static const char* AP_SSID = "esp32-garden-1";
static const char* AP_PASS = "begemotic1";

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static const uint32_t SERIAL_BAUD = 115200;
static const uint8_t PUMP_COUNT = 16;
static const uint8_t PUMP_NAME_MAX = 24;

static const uint32_t STORE_MAGIC = 0x47445231UL;  // "GDR1"
static const uint16_t STORE_VERSION = 1;
static const uint32_t NVS_AUTOSAVE_SEC = 300;  // snapshot countdowns every 5 min

// Active LOW: LOW = relay ON, HIGH = relay OFF (typical modules).
static const bool RELAY_ACTIVE_LOW = true;

// Module 1: IN1..IN8, Module 2: IN1..IN8
static const uint8_t RELAY_PINS[PUMP_COUNT] = {
  13, 12, 14, 27, 26, 25, 33, 32,
  4, 16, 17, 5, 18, 19, 21, 22
};

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

struct PumpConfig {
  char name[PUMP_NAME_MAX];
  bool enabled;
  uint16_t durationSec;
  uint16_t intervalHours;   // typical: 48 = every 2 days
  uint32_t dueInSec;        // countdown to next auto water (frozen when disabled)
  uint32_t wateredAgoSec;   // seconds since last water (UINT32_MAX = never)
};

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint16_t pumpCount;
  PumpConfig pumps[PUMP_COUNT];
  int8_t lastWateredIndex;
  uint8_t reserved[3];
};

PumpConfig pumps[PUMP_COUNT];
int8_t lastWateredIndex = -1;

int8_t activePump = -1;
uint32_t waterUntilMs = 0;

bool configDirty = false;
uint32_t lastTickMs = 0;
uint32_t secSinceNvsSave = 0;

uint32_t intervalToSec(uint16_t intervalHours) {
  return (uint32_t)intervalHours * 3600UL;
}

void markDirty() {
  configDirty = true;
}

void initPumpsDefaults() {
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    snprintf(pumps[i].name, PUMP_NAME_MAX, "Насос %u", i + 1);
    pumps[i].enabled = false;
    pumps[i].durationSec = 30;
    pumps[i].intervalHours = 48;
    pumps[i].dueInSec = intervalToSec(pumps[i].intervalHours);
    pumps[i].wateredAgoSec = UINT32_MAX;
  }
  lastWateredIndex = -1;
}

bool saveStateToNvs() {
  PersistedState st;
  memset(&st, 0, sizeof(st));
  st.magic = STORE_MAGIC;
  st.version = STORE_VERSION;
  st.pumpCount = PUMP_COUNT;
  memcpy(st.pumps, pumps, sizeof(pumps));
  st.lastWateredIndex = lastWateredIndex;

  bool ok = prefs.putBytes("state", &st, sizeof(st)) == sizeof(st);
  if (ok) {
    configDirty = false;
    secSinceNvsSave = 0;
    Serial.println("[nvs] state saved");
  } else {
    Serial.println("[nvs] save FAILED");
  }
  return ok;
}

bool loadStateFromNvs() {
  size_t len = prefs.getBytesLength("state");
  if (len != sizeof(PersistedState)) {
    Serial.print("[nvs] no valid state (len=");
    Serial.print(len);
    Serial.println("), using defaults");
    return false;
  }

  PersistedState st;
  if (prefs.getBytes("state", &st, sizeof(st)) != sizeof(st)) {
    Serial.println("[nvs] read failed");
    return false;
  }
  if (st.magic != STORE_MAGIC || st.version != STORE_VERSION || st.pumpCount != PUMP_COUNT) {
    Serial.println("[nvs] magic/version mismatch, using defaults");
    return false;
  }

  memcpy(pumps, st.pumps, sizeof(pumps));
  lastWateredIndex = st.lastWateredIndex;
  if (lastWateredIndex < -1 || lastWateredIndex >= (int8_t)PUMP_COUNT) {
    lastWateredIndex = -1;
  }

  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    pumps[i].name[PUMP_NAME_MAX - 1] = '\0';
    if (pumps[i].durationSec < 1) pumps[i].durationSec = 1;
    if (pumps[i].durationSec > 3600) pumps[i].durationSec = 3600;
    if (pumps[i].intervalHours < 1) pumps[i].intervalHours = 1;
    if (pumps[i].intervalHours > 8760) pumps[i].intervalHours = 8760;
    uint32_t maxDue = intervalToSec(pumps[i].intervalHours);
    if (pumps[i].dueInSec > maxDue) pumps[i].dueInSec = maxDue;
  }

  Serial.println("[nvs] state loaded");
  return true;
}

void saveStateIfNeeded(bool force) {
  if (force) {
    saveStateToNvs();
    return;
  }
  // Periodic snapshot so dueInSec / wateredAgo survive reboot (without wearing NVS every second).
  if (secSinceNvsSave >= NVS_AUTOSAVE_SEC) {
    saveStateToNvs();
  }
}

void resetPumpSchedule(uint8_t index) {
  if (index >= PUMP_COUNT) return;
  pumps[index].dueInSec = intervalToSec(pumps[index].intervalHours);
  pumps[index].wateredAgoSec = 0;
  lastWateredIndex = (int8_t)index;
  markDirty();
}

void relayWrite(uint8_t index, bool on) {
  if (index >= PUMP_COUNT) return;
  uint8_t level;
  if (RELAY_ACTIVE_LOW) {
    level = on ? LOW : HIGH;
  } else {
    level = on ? HIGH : LOW;
  }
  digitalWrite(RELAY_PINS[index], level);
}

void allRelaysOff() {
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    relayWrite(i, false);
  }
}

void initRelays() {
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
  }
  allRelaysOff();
  activePump = -1;
  waterUntilMs = 0;
}

bool isWateringActive() {
  return activePump >= 0 && activePump < PUMP_COUNT;
}

uint32_t wateringRemainSec() {
  if (!isWateringActive()) return 0;
  uint32_t now = millis();
  if ((int32_t)(waterUntilMs - now) <= 0) return 0;
  return (waterUntilMs - now + 999UL) / 1000UL;
}

String wateringToJson() {
  String json = "{";
  json += "\"active\":";
  json += isWateringActive() ? "true" : "false";
  json += ",\"pump\":";
  if (isWateringActive()) {
    json += String(activePump);
  } else {
    json += "null";
  }
  json += ",\"remainSec\":";
  json += String(wateringRemainSec());
  json += '}';
  return json;
}

void stopWatering() {
  if (isWateringActive()) {
    Serial.print("[water] stop pump=");
    Serial.println(activePump);
    relayWrite((uint8_t)activePump, false);
  }
  activePump = -1;
  waterUntilMs = 0;
}

bool startWater(uint8_t pump) {
  if (pump >= PUMP_COUNT) return false;
  if (isWateringActive()) return false;

  allRelaysOff();
  relayWrite(pump, true);

  activePump = (int8_t)pump;
  waterUntilMs = millis() + (uint32_t)pumps[pump].durationSec * 1000UL;
  resetPumpSchedule(pump);

  Serial.print("[water] start pump=");
  Serial.print(pump);
  Serial.print(" pin=");
  Serial.print(RELAY_PINS[pump]);
  Serial.print(" sec=");
  Serial.println(pumps[pump].durationSec);
  return true;
}

void updateWatering() {
  if (!isWateringActive()) return;
  if ((int32_t)(millis() - waterUntilMs) >= 0) {
    stopWatering();
  }
}

// Tick once per second: countdowns, auto schedule, NVS autosave.
void updateScheduleTick() {
  uint32_t now = millis();
  if (lastTickMs == 0) {
    lastTickMs = now;
    return;
  }
  while ((int32_t)(now - lastTickMs) >= 1000) {
    lastTickMs += 1000;
    secSinceNvsSave++;

    for (uint8_t i = 0; i < PUMP_COUNT; i++) {
      if (pumps[i].wateredAgoSec != UINT32_MAX && pumps[i].wateredAgoSec < UINT32_MAX - 1) {
        pumps[i].wateredAgoSec++;
      }
      if (!pumps[i].enabled) continue;
      if (pumps[i].dueInSec > 0) {
        pumps[i].dueInSec--;
      }
    }

    // Start the most overdue enabled pump when idle.
    if (!isWateringActive()) {
      int8_t best = -1;
      uint32_t bestOverdue = 0;
      for (uint8_t i = 0; i < PUMP_COUNT; i++) {
        if (!pumps[i].enabled) continue;
        if (pumps[i].dueInSec > 0) continue;
        // Overdue score: prefer longest-waiting (highest wateredAgo, or never).
        uint32_t overdue = (pumps[i].wateredAgoSec == UINT32_MAX)
                               ? UINT32_MAX
                               : pumps[i].wateredAgoSec;
        if (best < 0 || overdue >= bestOverdue) {
          best = (int8_t)i;
          bestOverdue = overdue;
        }
      }
      if (best >= 0) {
        Serial.print("[schedule] auto water pump=");
        Serial.println(best);
        if (startWater((uint8_t)best)) {
          saveStateToNvs();
        }
      }
    }

    saveStateIfNeeded(false);
  }
}

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out += c;
    }
  }
  return out;
}

String pumpsToJson() {
  String json = "[";
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    if (i) json += ',';
    json += '{';
    json += "\"name\":\"" + jsonEscape(String(pumps[i].name)) + "\",";
    json += "\"enabled\":";
    json += pumps[i].enabled ? "true" : "false";
    json += ",\"durationSec\":";
    json += String(pumps[i].durationSec);
    json += ",\"intervalHours\":";
    json += String(pumps[i].intervalHours);
    json += ",\"dueInSec\":";
    json += String(pumps[i].dueInSec);
    json += ",\"wateredAgoSec\":";
    if (pumps[i].wateredAgoSec == UINT32_MAX) {
      json += "null";
    } else {
      json += String(pumps[i].wateredAgoSec);
    }
    json += '}';
  }
  json += ']';
  return json;
}

String lastWateredToJson() {
  if (lastWateredIndex < 0 || lastWateredIndex >= PUMP_COUNT) {
    return "null";
  }
  uint8_t i = (uint8_t)lastWateredIndex;
  if (pumps[i].wateredAgoSec == UINT32_MAX) {
    return "null";
  }
  String json = "{";
  json += "\"index\":";
  json += String(lastWateredIndex);
  json += ",\"name\":\"" + jsonEscape(String(pumps[i].name)) + "\",";
  json += "\"agoSec\":";
  json += String(pumps[i].wateredAgoSec);
  json += '}';
  return json;
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
}

void handleStatus() {
  String body = "{";
  body += "\"apSsid\":\"" + jsonEscape(String(AP_SSID)) + "\",";
  body += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  body += "\"uptimeSec\":";
  body += String(millis() / 1000UL);
  body += ",\"pumpCount\":";
  body += String(PUMP_COUNT);
  body += ",\"watering\":";
  body += wateringToJson();
  body += ",\"lastWatered\":";
  body += lastWateredToJson();
  body += ",\"pumps\":";
  body += pumpsToJson();
  body += '}';
  server.send(200, "application/json; charset=utf-8", body);
}

bool extractBool(const String& body, const String& key, bool fallback) {
  String needle = "\"" + key + "\"";
  int keyPos = body.indexOf(needle);
  if (keyPos < 0) return fallback;
  int colon = body.indexOf(':', keyPos + needle.length());
  if (colon < 0) return fallback;
  int i = colon + 1;
  while (i < (int)body.length() && isspace((unsigned char)body[i])) i++;
  if (body.substring(i, i + 4) == "true") return true;
  if (body.substring(i, i + 5) == "false") return false;
  return fallback;
}

long extractNumberAfter(const String& body, int from, const String& key, long fallback) {
  String needle = "\"" + key + "\"";
  int keyPos = body.indexOf(needle, from);
  if (keyPos < 0) return fallback;
  int colon = body.indexOf(':', keyPos + needle.length());
  if (colon < 0) return fallback;
  int i = colon + 1;
  while (i < (int)body.length() && isspace((unsigned char)body[i])) i++;
  return body.substring(i).toInt();
}

bool extractQuotedString(const String& obj, const char* key, char* dest, size_t destSize) {
  String needle = String("\"") + key + "\"";
  int keyPos = obj.indexOf(needle);
  if (keyPos < 0) return false;
  int colon = obj.indexOf(':', keyPos + needle.length());
  if (colon < 0) return false;
  int q1 = obj.indexOf('"', colon + 1);
  if (q1 < 0) return false;

  String out;
  out.reserve(PUMP_NAME_MAX);
  for (int i = q1 + 1; i < (int)obj.length(); i++) {
    char c = obj[i];
    if (c == '\\' && i + 1 < (int)obj.length()) {
      out += obj[i + 1];
      i++;
      continue;
    }
    if (c == '"') break;
    out += c;
  }

  out.trim();
  if (out.length() == 0) return false;
  out.toCharArray(dest, destSize);
  return true;
}

void handleConfig() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Method not allowed\"}");
    return;
  }

  String body = server.arg("plain");
  if (body.length() == 0) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Empty body\"}");
    return;
  }

  int searchFrom = body.indexOf('[');
  if (searchFrom < 0) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"pumps array missing\"}");
    return;
  }

  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    int objStart = body.indexOf('{', searchFrom);
    if (objStart < 0) break;
    int objEnd = body.indexOf('}', objStart);
    if (objEnd < 0) break;
    String obj = body.substring(objStart, objEnd + 1);

    bool wasEnabled = pumps[i].enabled;
    uint16_t oldInterval = pumps[i].intervalHours;

    char newName[PUMP_NAME_MAX];
    if (extractQuotedString(obj, "name", newName, PUMP_NAME_MAX)) {
      strncpy(pumps[i].name, newName, PUMP_NAME_MAX - 1);
      pumps[i].name[PUMP_NAME_MAX - 1] = '\0';
    }

    pumps[i].enabled = extractBool(obj, "enabled", pumps[i].enabled);

    long duration = extractNumberAfter(obj, 0, "durationSec", pumps[i].durationSec);
    long interval = extractNumberAfter(obj, 0, "intervalHours", pumps[i].intervalHours);
    if (duration < 1) duration = 1;
    if (duration > 3600) duration = 3600;
    if (interval < 1) interval = 1;
    if (interval > 8760) interval = 8760;
    pumps[i].durationSec = (uint16_t)duration;
    pumps[i].intervalHours = (uint16_t)interval;

    uint32_t newIntervalSec = intervalToSec(pumps[i].intervalHours);

    // Newly enabled: wait a full interval before first auto water.
    if (!wasEnabled && pumps[i].enabled) {
      pumps[i].dueInSec = newIntervalSec;
    } else if (pumps[i].intervalHours != oldInterval) {
      if (pumps[i].dueInSec > newIntervalSec) {
        pumps[i].dueInSec = newIntervalSec;
      }
    }

    Serial.print("[config] pump ");
    Serial.print(i);
    Serial.print(" enabled=");
    Serial.print(pumps[i].enabled);
    Serial.print(" duration=");
    Serial.print(pumps[i].durationSec);
    Serial.print(" intervalH=");
    Serial.print(pumps[i].intervalHours);
    Serial.print(" dueInSec=");
    Serial.println(pumps[i].dueInSec);

    searchFrom = objEnd + 1;
  }

  markDirty();
  bool saved = saveStateToNvs();

  String resp = "{\"ok\":true,\"message\":\"";
  resp += saved ? "Сохранено" : "Сохранено (NVS ошибка)";
  resp += "\",\"lastWatered\":";
  resp += lastWateredToJson();
  resp += ",\"watering\":";
  resp += wateringToJson();
  resp += ",\"pumps\":";
  resp += pumpsToJson();
  resp += '}';
  server.send(200, "application/json; charset=utf-8", resp);
}

void handleWater() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Method not allowed\"}");
    return;
  }

  int pump = -1;
  if (server.hasArg("pump")) {
    pump = server.arg("pump").toInt();
  } else if (server.hasArg("zone")) {
    pump = server.arg("zone").toInt();
  } else {
    String body = server.arg("plain");
    pump = (int)extractNumberAfter(body, 0, "pump", -1);
    if (pump < 0) pump = (int)extractNumberAfter(body, 0, "zone", -1);
  }

  if (pump < 0 || pump >= PUMP_COUNT) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Bad pump\"}");
    return;
  }

  if (isWateringActive()) {
    String resp = "{\"ok\":false,\"message\":\"busy\",\"watering\":";
    resp += wateringToJson();
    resp += ",\"lastWatered\":";
    resp += lastWateredToJson();
    resp += '}';
    server.send(409, "application/json; charset=utf-8", resp);
    return;
  }

  if (!startWater((uint8_t)pump)) {
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Failed to start watering\"}");
    return;
  }

  saveStateToNvs();

  String resp = "{\"ok\":true,\"message\":\"Watering pump ";
  resp += String(pump + 1);
  resp += "\",\"pump\":";
  resp += String(pump);
  resp += ",\"durationSec\":";
  resp += String(pumps[pump].durationSec);
  resp += ",\"active\":true";
  resp += ",\"watering\":";
  resp += wateringToJson();
  resp += ",\"lastWatered\":";
  resp += lastWateredToJson();
  resp += ",\"pumps\":";
  resp += pumpsToJson();
  resp += '}';
  server.send(200, "application/json; charset=utf-8", resp);
}

void handleNotFound() {
  handleRoot();
}

void printBanner(bool apOk) {
  Serial.println();
  Serial.println("========================================");
  Serial.println(" GardenESP ready");
  Serial.println("========================================");
  Serial.print(" Serial baud: ");
  Serial.println(SERIAL_BAUD);
  Serial.print(" SoftAP: ");
  Serial.println(apOk ? "OK" : "FAILED");
  Serial.print(" SSID: ");
  Serial.println(AP_SSID);
  Serial.print(" PASS: ");
  Serial.println(AP_PASS);
  Serial.print(" IP:   ");
  Serial.println(WiFi.softAPIP());
  Serial.print(" Pumps: ");
  Serial.println(PUMP_COUNT);
  Serial.print(" Relay: ");
  Serial.println(RELAY_ACTIVE_LOW ? "Active LOW" : "Active HIGH");
  Serial.println(" Schedule: auto by dueInSec + NVS");
  Serial.println(" Open in browser:");
  Serial.println("   http://192.168.4.1");
  Serial.println(" (use http, not https)");
  Serial.println("========================================");
  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);
  while (Serial.available()) {
    Serial.read();
  }
  Serial.println();
  Serial.println();
  Serial.println("[boot] GardenESP starting...");
  Serial.print("[boot] Set Serial Monitor baud to ");
  Serial.println(SERIAL_BAUD);

  initPumpsDefaults();
  prefs.begin("garden", false);
  loadStateFromNvs();
  initRelays();

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);

  dnsServer.start(53, "*", AP_IP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/water", HTTP_POST, handleWater);
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/gen_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/connecttest.txt", HTTP_GET, handleRoot);
  server.on("/ncsi.txt", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  lastTickMs = millis();
  printBanner(apOk);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  updateWatering();
  updateScheduleTick();
}
