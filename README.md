# UrbanistDiabeticDash

A diabetic CGM dashboard based on Eastside Urbanism's Transit Tracker hardware.

## Hardware

- **MCU**: Adafruit Matrix Portal S3 (ESP32-S3, WiFi built-in)
- **Display**: 2× Waveshare RGB-Matrix-P2.5-64x32 HUB75, chained side-by-side → 128×32 total
- **Power**: 5V/2A USB-C

## Quick start

1. Open `installer/index.html` in Chrome or Edge and click **Install Firmware**.
2. After flashing, connect to the **DiabeticDash-Setup** WiFi network.
3. Open **192.168.4.1** in your browser and enter your WiFi and Dexcom credentials.
4. Save — the dashboard will appear on the display within a minute.

## Building from source

```bash
# Install PlatformIO, then:
pio run -t uploadfs   # flash LittleFS (config web UI)
pio run -t upload     # flash firmware
```

## Architecture

```
[Dexcom Share API]
        ↓ HTTPS, polled every 60 s
[ESP32-S3 on Matrix Portal S3]
  ├── DexcomSource  (auth + fetch)
  ├── ConfigStore   (NVS credentials)
  ├── DashWebServer (on-device config UI)
  └── DisplayRenderer → 128×32 HUB75 display
```

`CGMSource` is an abstract interface — future sources (Nightscout, LibreLink) only need to implement `fetch()`.

## License

MIT
