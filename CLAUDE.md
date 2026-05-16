# UrbanistDiabeticDash

Real-time CGM (continuous glucose monitor) display for the **Adafruit Matrix Portal S3** — an ESP32-S3 driving two chained 64×32 HUB75 LED matrices (128×32 total). Displays glucose value, trend arrow, wave animation, sparkline, and clock.

## Hardware

- **Board**: Adafruit Matrix Portal S3 (ESP32-S3, 8 MB flash, built-in WiFi)
- **Display**: 2× Waveshare RGB-Matrix-P2.5-64×32, chained → 128×32 pixels
- **IMU**: LIS3DH at I2C 0x19 (auto-rotate)
- **Power**: 5 V/2 A USB-C

## Project Structure

```
src/
  main.cpp              — setup/loop, WiFi, state machine
  web_server.cpp        — AsyncWebServer: config UI, OTA endpoints, status
  config_store.cpp      — NVS (Preferences) load/save for DashConfig
  display_renderer.cpp  — all pixel drawing: stars, wave, glucose, arrow, sparkline
  dexcom_source.cpp     — Dexcom Share HTTPS polling
  nightscout_source.cpp — Nightscout HTTPS polling
include/
  config_store.h        — DashConfig struct (all settings)
  web_server.h
  display_renderer.h
  version.h             — FIRMWARE_VERSION constant (injected by CI, "dev" locally)
data/                   — LittleFS web UI (flashed separately)
  index.html            — config + OTA + export/import UI
  style.css
sim/
  index.html            — pixel-perfect browser simulator (no server needed)
installer/              — pre-built binaries + esp-web-tools installer (GitHub Pages)
```

## Build & Flash

**Toolchain**: PlatformIO + Arduino framework for ESP32.

```bash
# Build firmware
pio run

# Build LittleFS image (web UI)
pio run --target buildfs

# Flash via USB (initial or after OTA breaks)
pio run --target upload          # firmware
pio run --target uploadfs        # web UI (LittleFS)

# Monitor serial
pio device monitor
```

First-time flash is easiest via the web installer at the GitHub Pages URL (uses esp-web-tools over USB/Chrome).

## OTA Updates

Two paths exist:

1. **Cloud OTA** (for end users): open the device config page → Firmware Update card → "Check for Update" → "Update Now". Device fetches `installer/firmware.bin` from the main branch on GitHub raw and flashes it via `HTTPUpdate`. Config (NVS) and web UI (LittleFS) are untouched — separate flash partitions.

2. **Manual HTTP upload** (fallback): `http://<device-ip>/update` — upload a `firmware.bin` directly. Also reachable via "Manual firmware upload" link on the config page.

For rapid dev iteration, build locally then POST to the running device:
```bash
pio run && curl -X POST http://<device-ip>/update \
  -F "firmware=@.pio/build/matrix-portal-s3/firmware.bin"
```

## Config

All settings are stored in NVS namespace `diabdash` via the ESP32 `Preferences` library. They survive firmware OTA updates (NVS is a separate partition).

`GET /config` returns all non-sensitive settings as JSON (passwords are write-only). `POST /config` accepts the same JSON; omitted fields keep their current values.

**Export/Import**: the device config page has "Export Config" (downloads `diabdash-config.json`) and "Import Config" (uploads a JSON file, POSTs to `/config`). The simulator's "Copy Config JSON" produces the same format — paste from sim → import on device, or vice versa.

## Simulator

Open `sim/index.html` directly in any browser — no server needed. Pixel-perfect port of the C++ rendering logic in plain JavaScript. Use it to preview display changes without flashing hardware.

- **Copy Config JSON** — exports current sim settings in device-compatible JSON format
- **Import Config** — load a `diabdash-config.json` exported from the device to mirror its exact settings

## CI/CD

`.github/workflows/build.yml` — triggers on push to `main`:
1. Computes version string (`YYYYMMDD-{sha}`) and injects it as `FIRMWARE_VERSION` compile flag
2. Builds firmware and LittleFS
3. Copies binaries to `installer/`
4. Stamps version into manifests and HTML
5. Commits updated binaries back to `main`
6. Deploys `installer/` to GitHub Pages

## Key Design Notes

- **Stars**: positions are fixed (deterministic hash with a constant slot `42`) — stars twinkle via a per-star sine wave but never relocate.
- **Wave**: animated via summed sine harmonics; position/brightness driven by `millis()`.
- **Display layout**: 128×32 px. Rows 0–21: content area (glucose, arrow, clock, stars). Rows 22–31: wave/status strip.
- **Colors**: stored as packed `0x00RRGGBB` uint32 in NVS; serialized as `#rrggbb` hex strings over HTTP.
- **No mDNS**: device IP is shown on the LED matrix during WiFi setup and on the status endpoint.
- **HTTPS**: `WiFiClientSecure` with `setInsecure()` (no cert pinning) — consistent with the local-network trust model.
