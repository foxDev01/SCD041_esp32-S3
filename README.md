# SCD41 ESP32-S3 Monitor

Firmware for an **ESP32-S3 DevKitC-1** that reads CO₂, temperature and humidity from a
**Sensirion SCD41** sensor (I²C) and exposes them through an embedded web dashboard and
MQTT. The web UI and serial console messages are in Russian.

## Features

- **Web dashboard** (port 80) with four tabs: Dashboard, Wi-Fi, MQTT, Calibration
- **CO₂ / temperature / humidity** readings updated every 2 seconds
- **Wi-Fi station mode** with credentials stored in NVS (Preferences)
- **Access Point fallback** (`SCD41-Monitor`) with a captive configuration portal when
  no Wi-Fi credentials are stored or the connection fails
- **MQTT publishing** of JSON telemetry every 10 seconds (optional)
- **Sensor calibration**: ASC (automatic self-calibration), forced recalibration (FRC),
  factory reset and measurement restart

## Hardware

| SCD41 | ESP32-S3 |
|-------|----------|
| SDA   | GPIO 8   |
| SCL   | GPIO 9   |
| VCC   | 3.3 V    |
| GND   | GND      |

I²C pins are defined at the top of `src/main.cpp`:

```cpp
#define I2C_SDA 8
#define I2C_SCL 9
```

## Requirements

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32-S3 DevKitC-1 board
- SCD41 sensor breakout

Dependencies:

- `knolleary/PubSubClient` (declared in `platformio.ini`)
- `ArduinoJson` (vendored in `lib/`)
- `SparkFun_SCD4x_Arduino_Library` (vendored in `lib/`)

## Configuration

### `platformio.ini`

The project env is `esp32-s3-devkitc-1` (Arduino framework, 240 MHz, QIO flash,
115200 baud monitor).

### Secrets

`platformio.ini` pulls in `secrets.ini` via `extra_configs`:

```ini
[platformio]
extra_configs = secrets.ini
```

`secrets.ini` is **gitignored**. Copy the template and fill it in:

```
cp secrets_example.ini secrets.ini
```

```ini
[env]
build_flags =
    -DWIFI_SSID=""
    -DWIFI_PASSWORD=""
```

> At runtime Wi-Fi credentials come from NVS, not from these build flags.

## Build and upload

```bash
# Build
pio run

# Upload to the board
pio run --target upload

# Open the serial monitor
pio device monitor
```

## Usage

### First boot (no saved Wi-Fi)

1. The board starts an access point: **SSID** `SCD41-Monitor`, **password** `12345678`.
2. Connect to it and open <http://192.168.4.1>.
3. Go to the **Wi-Fi** tab, enter your network credentials and save. The board reboots
   and joins your network.

The AP mode can also be re-entered by saving invalid credentials; the fallback triggers
automatically when the connection attempt fails.

### Web interface

| Tab | What it does |
|-----|--------------|
| **Дашборд** (Dashboard) | Live CO₂ / temperature / humidity, sensor / Wi-Fi / MQTT status |
| **Wi-Fi** | Set SSID and password, shows current mode and IP |
| **MQTT** | Enable MQTT, set server, port, username, password and topic |
| **Калибровка** (Calibration) | Toggle ASC, fresh-air FRC (400 ppm), forced recalibration, factory reset, sensor restart |

### HTTP API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Dashboard HTML |
| `/api/data` | GET | `co2`, `temp`, `humidity`, `connected`, `mqtt_enabled`, `mqtt_connected`, `ap_mode` |
| `/api/wifi/info` | GET | Mode (AP/STA), IP, RSSI or station count |
| `/api/wifi/config` | POST | `{ "ssid": "...", "password": "..." }` — saves and reboots |
| `/api/mqtt/config` | GET | Current MQTT settings |
| `/api/mqtt/config` | POST | Save MQTT settings and reboot |
| `/api/sensor/info` | GET | ASC state and serial number |
| `/api/calibration/asc` | POST | `{ "enabled": true/false }` — set and persist ASC |
| `/api/calibration/fresh_air` | POST | Forced recalibration to 400 ppm |
| `/api/calibration/force` | POST | `{ "value": 400..2000 }` — forced recalibration |
| `/api/calibration/factory_reset` | POST | SCD41 factory reset |
| `/api/sensor/restart` | POST | Restart periodic measurement |

### MQTT payload

Published to the configured topic (default `scd41/sensors`) every 10 seconds while
connected in station mode:

```json
{
  "co2": 812,
  "temperature": 23.4,
  "humidity": 41.2,
  "timestamp": 123456
}
```

`timestamp` is the ESP32 `millis()` value, not a wall-clock time.

## Implementation notes

- **Calibration commands** (FRC, ASC, serial number, factory reset) are only accepted by
  the SCD41 while periodic measurement is stopped. The firmware stops measurement
  (`beginSensorConfig()`), runs the command, then restarts it (`endSensorConfig()`).
- **ASC persistence**: the driver's `begin()` forces ASC on at every boot. The chosen ASC
  state is therefore mirrored in NVS (`sensor` namespace) and passed to `begin()`.
- **Preferences namespaces**: `wifi`, `mqtt`, `sensor`.

## Project layout

```
SCD041_esp32-S3/
├─ src/main.cpp          # entire application
├─ include/              # headers (PlatformIO template README)
├─ lib/                  # vendored ArduinoJson, PubSubClient, SparkFun SCD4x
├─ test/                 # tests (PlatformIO template README)
├─ platformio.ini        # build configuration
├─ secrets_example.ini   # template for local secrets
└─ secrets.ini           # local secrets (gitignored)
```

## Calibration tips

- Let the sensor run for **at least 3 minutes** before a forced/fresh-air recalibration.
- Use **400 ppm** as the target for fresh outdoor air.
- ASC expects the sensor to see fresh air regularly (e.g. once a day); it is enabled by
  default.

## License

Released under the [MIT License](LICENSE).
