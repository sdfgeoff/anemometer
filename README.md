# Anemometer (ESP32 + RPR220 + React UI)

First-pass anemometer firmware and web UI.

- Wind source abstraction with `Dummy` and `RPR220` implementations.
- 30-second samples stored in a 7-day ring buffer on ESP32 RAM.
- API endpoints for current speed, last 24h history, and last 7d history.
- React/Vite dashboard embedded into firmware binary as static assets.

## Repo Layout

- `firmware/` Arduino sketch and source files.
- `web/` React + Vite frontend.
- `docs/WIRING.md` hardware wiring diagram.
- `docs/rpr-220-datasheet.pdf` local copy of the ROHM RPR-220 datasheet.
- `scripts/build_web_assets.sh` build frontend and generate embedded C++ assets.
- `scripts/compile_firmware.sh` compile firmware with `arduino-cli`.
- `scripts/upload_firmware.sh` compile + upload firmware.

## Firmware Behavior

- Sampling interval: `30s`
- Last 24h graph data: `2,880` points
- Last week history: up to `20,160` points
- API:
  - `GET /api/health`
  - `GET /api/current`
  - `GET /api/history?range=24h`
  - `GET /api/history?range=week`
  - `GET /api/wifi/status`
  - `POST /api/wifi/config` (`ssid`, `password`)
  - `POST /api/wifi/clear`

## Wi-Fi Provisioning Flow

1. Device boots and starts an open AP: `anemometer`.
2. Connect phone/laptop to that AP and open `http://192.168.4.1`.
3. Enter SSID/password in the web page and submit.
4. Device saves credentials and attempts STA connection.
5. On successful STA connect, firmware prints IP over serial as:
   - `[wifi] STA connected. IP=...`

By default AP is disabled after STA connect. If you want AP to remain active, set
`kKeepApAfterStaConnect = true` in `firmware/firmware.ino`.

## Local Web Dev (dummy source)

```bash
cd web
npm install
npm run dev
```

In Vite dev mode, API calls target same host by default. If needed, add a Vite proxy to your ESP IP.

## Build Embedded Web Assets

```bash
./scripts/build_web_assets.sh
```

This generates:
- `firmware/src/web_assets.generated.h`
- `firmware/src/web_assets.generated.cpp`

These files are ignored in git and regenerated as needed.

## Arduino CLI Setup

`arduino-cli` is required (not installed in this environment at time of scaffold).

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

For Lolin Lite, find and confirm your board FQBN:

```bash
arduino-cli board listall | rg -i "lolin|wemos"
arduino-cli board list
```

Examples:

```bash
./scripts/compile_firmware.sh
PORT=/dev/ttyUSB0 ./scripts/upload_firmware.sh
```

Defaults use `FQBN=esp32:esp32:lolin32-lite` and can be overridden per command.

## Swapping Dummy -> Real Sensor

Edit constants in `firmware/firmware.ino`:
- `kUseDummySource` -> `false`
- `kRpr220Pin`
- `kPulsesPerRevolution` (currently `12`)
- `kMpsPerHz` (calibration)

## Serial Logging

Firmware logs boot, Wi-Fi state changes, and each 30-second sample over serial at `115200`.
