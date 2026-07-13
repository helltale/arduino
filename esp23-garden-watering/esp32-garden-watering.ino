#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "web_ui.h"

static const char* AP_SSID = "esp32-garden-1";
static const char* AP_PASS = "begemotic1";

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static const uint32_t SERIAL_BAUD = 115200;
static const uint8_t PUMP_COUNT = 14;
static const uint8_t PUMP_NAME_MAX = 24;

WebServer server(80);
DNSServer dnsServer;

struct PumpConfig {
  char name[PUMP_NAME_MAX];
  bool enabled;
  uint16_t durationSec;
  uint16_t intervalHours;  // typical: 48 = every 2 days
};

PumpConfig pumps[PUMP_COUNT];
int8_t lastWateredIndex = -1;
uint32_t lastWateredAtMs = 0;

void initPumps() {
  for (uint8_t i = 0; i < PUMP_COUNT; i++) {
    snprintf(pumps[i].name, PUMP_NAME_MAX, "Насос %u", i + 1);
    pumps[i].enabled = false;
    pumps[i].durationSec = 30;
    pumps[i].intervalHours = 48;  // once every 2 days
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
    json += '}';
  }
  json += ']';
  return json;
}

String lastWateredToJson() {
  if (lastWateredIndex < 0 || lastWateredIndex >= PUMP_COUNT) {
    return "null";
  }
  uint32_t agoSec = (millis() - lastWateredAtMs) / 1000UL;
  String json = "{";
  json += "\"index\":";
  json += String(lastWateredIndex);
  json += ",\"name\":\"" + jsonEscape(String(pumps[lastWateredIndex].name)) + "\",";
  json += "\"agoSec\":";
  json += String(agoSec);
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
    if (interval > 8760) interval = 8760;  // up to 1 year
    pumps[i].durationSec = (uint16_t)duration;
    pumps[i].intervalHours = (uint16_t)interval;

    Serial.print("[config] pump ");
    Serial.print(i);
    Serial.print(" enabled=");
    Serial.print(pumps[i].enabled);
    Serial.print(" duration=");
    Serial.print(pumps[i].durationSec);
    Serial.print(" intervalH=");
    Serial.println(pumps[i].intervalHours);

    searchFrom = objEnd + 1;
  }

  String resp = "{\"ok\":true,\"message\":\"Saved (RAM)\",\"lastWatered\":";
  resp += lastWateredToJson();
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

  lastWateredIndex = (int8_t)pump;
  lastWateredAtMs = millis();

  Serial.print("[water] stub pump=");
  Serial.print(pump);
  Serial.print(" sec=");
  Serial.println(pumps[pump].durationSec);

  String resp = "{\"ok\":true,\"message\":\"Water request for pump ";
  resp += String(pump + 1);
  resp += " (stub)\",\"pump\":";
  resp += String(pump);
  resp += ",\"lastWatered\":";
  resp += lastWateredToJson();
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

  initPumps();

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

  printBanner(apOk);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}
