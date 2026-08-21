# arduino

Скетчи и библиотеки для Arduino / ESP32.

## Структура

| Папка | Описание |
| --- | --- |
| [`esp32-garden-watering/`](esp32-garden-watering/) | Автополив на ESP32: Wi‑Fi AP, веб‑интерфейс, до 16 насосов |
| [`solar-panel/`](solar-panel/) | Слежение за солнцем (Keyestudio sun follower) |
| [`Libraries/`](Libraries/) | ZIP‑библиотеки для скетчей |

## esp32-garden-watering

Система автополива на ESP32 (GardenESP).

- Точка доступа Wi‑Fi и веб‑UI для управления насосами
- До 16 реле/насосов с именами, интервалом и длительностью полива
- Расписание и состояние сохраняются в NVS
- Captive DNS для удобного входа в настройки

Откройте `esp32-garden-watering/esp32-garden-watering.ino` в Arduino IDE (плата ESP32). Подробнее — в [`esp32-garden-watering/README.md`](esp32-garden-watering/README.md).

## solar-panel

Скетч слежения за солнцем на базе Keyestudio sun follower:

- 4 фоторезистора и 2 сервопривода (азимут / высота)
- BH1750 — освещённость
- DHT11 — температура и влажность
- LCD 1602 (I2C) — вывод значений
- Кнопка меняет шаг поворота серво

Библиотеки лежат в `Libraries/` (BH1750, DHT11, LiquidCrystal_I2C, Servo, Wire). Установите их через Sketch → Include Library → Add .ZIP Library.

## Требования

- [Arduino IDE](https://www.arduino.cc/en/software) (или совместимая среда)
- Для автополива: поддержка плат ESP32
- Для солнечной панели: Arduino-совместимая плата и соответствующие датчики/серво
