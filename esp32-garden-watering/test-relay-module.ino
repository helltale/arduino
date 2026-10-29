
#include <Arduino.h>

constexpr uint8_t RELAY_COUNT = 16;

// Подключение реле IN1–IN16 к ESP32
const uint8_t RELAY_PINS[RELAY_COUNT] = {
  26, 23, 27, 22,
  32, 21, 33, 19,
  14, 18, 13, 17,
   4, 16,  5, 25
};

// Релейный модуль Active LOW
constexpr uint8_t RELAY_ON  = LOW;
constexpr uint8_t RELAY_OFF = HIGH;

// Время включения реле — 3 секунды
constexpr unsigned long TEST_TIME_MS = 3000;

int activeRelay = -1;
unsigned long relayStartedAt = 0;

// Выключить реле
void relayOff(uint8_t index) {
  if (index >= RELAY_COUNT) return;

  digitalWrite(RELAY_PINS[index], RELAY_OFF);
}

// Включить реле
void relayOn(uint8_t index) {
  if (index >= RELAY_COUNT) return;

  digitalWrite(RELAY_PINS[index], RELAY_ON);
}

// Выключить все реле
void allRelaysOff() {
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    relayOff(i);
  }

  activeRelay = -1;
}

// Вывести состояния GPIO
void printPinStates() {
  Serial.println();
  Serial.println("GPIO states:");

  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    Serial.print("IN");
    Serial.print(i + 1);
    Serial.print(" -> GPIO");
    Serial.print(RELAY_PINS[i]);
    Serial.print(" = ");

    Serial.println(
      digitalRead(RELAY_PINS[i]) == HIGH
        ? "HIGH"
        : "LOW"
    );
  }
}

// Проверить конкретное реле
void testRelay(uint8_t relayNumber) {
  if (relayNumber < 1 ||
      relayNumber > RELAY_COUNT) {
    Serial.println("Invalid relay number");
    return;
  }

  // Сначала выключаем все реле
  allRelaysOff();

  delay(200);

  uint8_t index = relayNumber - 1;

  Serial.print("Switching ON relay ");
  Serial.println(relayNumber);

  relayOn(index);

  activeRelay = index;
  relayStartedAt = millis();
}

void setup() {
  // Сначала задаём HIGH, потом OUTPUT
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
    pinMode(RELAY_PINS[i], OUTPUT);
  }

  Serial.begin(115200);
  Serial.setTimeout(100);

  allRelaysOff();

  delay(1000);

  Serial.println();
  Serial.println("ESP32 RELAY CONTROLLER");
  Serial.println("Mode: ACTIVE LOW");
  Serial.println("Commands:");
  Serial.println("1..16 - test selected relay");
  Serial.println("off   - switch all OFF");
  Serial.println("state - show GPIO states");

  printPinStates();
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command == "off") {
      allRelaysOff();
      Serial.println("ALL RELAYS -> OFF");
    }
    else if (command == "state") {
      printPinStates();
    }
    else {
      int number = command.toInt();

      if (number >= 1 && number <= 16) {
        testRelay(number);
      }
      else {
        Serial.println("Unknown command");
      }
    }
  }

  // Автоматическое выключение через 3 секунды
  if (activeRelay >= 0 &&
      millis() - relayStartedAt >= TEST_TIME_MS) {

    Serial.print("Switching OFF relay ");
    Serial.println(activeRelay + 1);

    relayOff(activeRelay);
    activeRelay = -1;
  }
}
