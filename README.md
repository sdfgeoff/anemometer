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

For Lolin Lite, set your board FQBN if needed:

```bash
arduino-cli board listall | rg -i lolin
```

Examples:

```bash
FQBN=esp32:esp32:esp32 ./scripts/compile_firmware.sh
PORT=/dev/ttyUSB0 FQBN=esp32:esp32:esp32 ./scripts/upload_firmware.sh
```

## Swapping Dummy -> Real Sensor

Edit constants in `firmware/anemometer.ino`:
- `kUseDummySource` -> `false`
- `kRpr220Pin`
- `kPulsesPerRevolution` (currently `12`)
- `kMpsPerHz` (calibration)

## Wi-Fi Mode

Default is ESP32 AP mode:
- SSID: `anemometer`
- Password: `windmeter123`

Change in `firmware/anemometer.ino`:
- `kUseApMode = false`
- `kStaSsid` / `kStaPassword`
