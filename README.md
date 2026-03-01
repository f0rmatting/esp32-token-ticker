# TokenTicker

[![Build](https://github.com/f0rmatting/esp32-token-ticker/actions/workflows/build.yml/badge.svg)](https://github.com/f0rmatting/esp32-token-ticker/actions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3%20|%20ESP32--C6-green?logo=espressif)](https://www.waveshare.com/esp32-s3-touch-lcd-1.47.htm)
[![License](https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey)](LICENSE)
[![LVGL](https://img.shields.io/badge/LVGL-9.2-pink)](https://lvgl.io/)

[中文文档](README_zh.md)

An open-source crypto desktop companion powered by ESP32. Real-time prices via HTTP polling, 24h candlestick charts, touch gestures, market mood LED, Apple HomeKit integration, and phone-based Wi-Fi setup — all on a 1.47" LCD you can leave running 24/7.

Supports up to **10 tokens** (BTC, ETH, SOL, SUI, PAXG, BNB, XRP, DOGE, CBETH, USDT) — configurable via the Wi-Fi provisioning portal. The device also acts as an Apple HomeKit Stateless Programmable Switch — price surge/crash alerts can trigger Home automations directly.

![WiFi Setup Screen](docs/images/wifi-setup.jpg)
![Boot Screen](docs/images/boosting.jpg)
![BTC Price View](docs/images/token-price1.jpg)
![PAXG Price View](docs/images/token-price2.jpg)
![System Info Panel](docs/images/system-info.jpg)

## Features

- **Real-Time Prices** — Focused token polls every 10s, background tokens every 10min; animated price updates with 48-point 24h candlestick chart (30min intervals)
- **Touch Gestures** (ESP32-S3) — Swipe left/right to toggle Info panel, swipe up/down to switch tokens
- **Dynamic Token Config** — Select 4–6 tokens from 10 supported coins via the Wi-Fi provisioning portal
- **Market Mood LED** — WS2812B breathing animation (green on gains, red on losses), bright flash on price ticks (ESP32-C6)
- **Apple HomeKit** — Stateless Programmable Switch in Apple Home; price surge (+5%) triggers SinglePress, crash (-5%) triggers DoublePress
- **System Info Panel** — Chip temperature arc, heap usage arc, Wi-Fi signal bars, HomeKit pairing status, and clock
- **SoftAP Provisioning** — No hardcoded Wi-Fi credentials; configure via captive portal from your phone
- **Boot Animation** — Logo with glass shine sweep effect during startup

## Hardware

Both boards share the same 1.47" LCD form factor and are supported from a unified codebase.

| | ESP32-S3 (Touch) | ESP32-C6 |
|---|---|---|
| Board | [Waveshare ESP32-S3-Touch-LCD-1.47](https://www.waveshare.com/esp32-s3-touch-lcd-1.47.htm) | [Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/esp32-c6-lcd-1.47.htm) |
| MCU | ESP32-S3 @ 240 MHz | ESP32-C6 @ 160 MHz |
| Display | 1.47" JD9853 LCD, 320x172 | 1.47" ST7789 LCD, 320x172 |
| Touch | AXS5106 capacitive touch (I2C) | — |
| Flash / PSRAM | 16 MB / 8 MB | 4 MB / — |
| LED | — | WS2812B RGB (1 pixel) |
| Input | Touch gestures + BOOT button | BOOT button |

## Controls

### Button (both boards)

| Action | Crypto View | Info Panel |
|--------|-------------|------------|
| Single click | Switch to next coin | Send HomeKit SinglePress |
| Double click | Toggle info panel | Toggle info panel |
| Long press (3s) | Reset HomeKit + enter Wi-Fi provisioning | Reset HomeKit + enter Wi-Fi provisioning |

### Touch Gestures (ESP32-S3 only)

| Gesture | Action |
|---------|--------|
| Swipe left | Open Info panel |
| Swipe right | Return to Crypto view |
| Swipe up | Next token |
| Swipe down | Previous token |

## Wi-Fi Provisioning

On first boot (or long-press BOOT for 3 seconds):

1. Device creates open AP: **TokenTicker**
2. Connect from your phone — captive portal opens automatically
3. Select your Wi-Fi network from the scanned list
4. Enter password, choose timezone, and select tokens
5. Device saves to NVS and reboots

Credentials, timezone, and token selection persist across power cycles.

## Apple HomeKit

The device registers as a **Stateless Programmable Switch** in Apple Home:

- **Setup code** — Derived from your device's MAC address, displayed on the Info panel
- **Pairing** — Open Apple Home > Add Accessory > enter the setup code shown on screen
- **Price alerts** — When the focused coin surges +5% (24h), a SinglePress event is sent; when it crashes -5%, a DoublePress event is sent
- **Automations** — Use these events to trigger Apple Home scenes (requires a Home Hub: HomePod, Apple TV, or iPad)

> With the market like this, even turning on the lights at home feels like burning money. So we hooked it up to HomeKit — when BTC crashes, the lights automatically turn off to save on electricity. Every little bit helps.

Long-press reset clears both Wi-Fi credentials and HomeKit pairing data.

## UI Layout

### Crypto View (default)

```
┌──────────────────────────────────────────┐
│ ┌─────────────────────┐ ┌──────────────┐ │
│ │  BTC  Bitcoin       │ │  ETH         │ │
│ │  $68,432.10         │ │  $3,841.20   │ │
│ │  +2.34%             │ │  -0.82%      │ │
│ │                     │ ├──────────────┤ │
│ │                     │ │  SUI         │ │
│ │  ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃ │ │  $1.23       │ │
│ └─────────────────────┘ └──────────────┘ │
└──────────────────────────────────────────┘
  Main card (swipe ↑↓)    Side cards (marquee)
```

### Info Panel (swipe left / double-click)

```
┌──────────────────────────────────────────┐
│ ┌──────────────────────────────────────┐ │
│ │              14:32:07                │ │
│ └──────────────────────────────────────┘ │
│ ┌────────────────┐ ┌──────────────────┐ │
│ │    SYSTEM       │ │ NETWORK   MyWiFi│ │
│ │  ╭──╮   ╭──╮   │ │ Wi-Fi 6 2.4G    │ │
│ │  │38│   │22│   │ │ ▐▐▐▐ -52 dBm    │ │
│ │  ╰──╯   ╰──╯   │ │                 │ │
│ │  TEMP   HEAP    │ │ SETUP 123-45-678│ │
│ └────────────────┘ └──────────────────┘ │
└──────────────────────────────────────────┘
```

## Quick Start (Pre-built Firmware)

No development environment needed — flash directly from your browser:

1. Download the latest firmware from [Releases](https://github.com/f0rmatting/esp32-token-ticker/releases)
2. Connect the board via USB-C
3. Open [ESP Web Flasher](https://espressif.github.io/esptool-js/) in Chrome/Edge
4. Click **Connect**, select the serial port
5. Flash the **factory bin** (single file, includes everything):

| Address | File |
|---------|------|
| `0x0` | `token_ticker-esp32s3-factory.bin` or `token_ticker-esp32c6-factory.bin` |

6. Click **Program** and wait for completion
7. On first boot the device enters Wi-Fi provisioning mode automatically

## Build from Source

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) v5.5+
- A supported Waveshare board (ESP32-S3-Touch-LCD-1.47 or ESP32-C6-LCD-1.47)
- USB-C cable

### Build with local ESP-IDF

```bash
# Clone the repository (--recursive for esp-homekit-sdk submodule)
git clone --recursive https://github.com/f0rmatting/esp32-token-ticker.git
cd esp32-token-ticker

# Build for ESP32-S3
idf.py set-target esp32s3
idf.py build

# Or for ESP32-C6
idf.py set-target esp32c6
idf.py build

# Flash and monitor
idf.py -p <PORT> flash monitor
```

### Build with Docker (no local ESP-IDF needed)

```bash
docker run --rm -v $PWD:/project -w /project espressif/idf:v5.5.3 \
  bash -c ". \$IDF_PATH/export.sh && idf.py set-target esp32s3 && idf.py build"
```

## Why Gate.io?

As a top-tier global exchange, Gate.io provides a significant technical advantage: its API is directly accessible from mainland China without the need for a VPN. For an always-on embedded device that demands reliable, uninterrupted price data, Gate.io is the most practical and robust choice among leading platforms.

## Project Structure

```
main/
├── token_ticker.c      Entry point (app_main)
├── board_config.h      Target-specific GPIO pin definitions (C6 / S3)
├── display.c/h         SPI + LCD panel + touch + LVGL initialization
├── ui.c                Main crypto UI, boot screen, price cards, touch gestures
├── ui_info.c           Info panel (time, temp arc, heap arc, WiFi, HomeKit)
├── ui_internal.h       Shared UI state and layout constants
├── ui.h                Public UI interface
├── button.c            Button handler (single/double/long press)
├── wifi.c/h            Wi-Fi STA connection + auto-reconnect
├── wifi_prov.c/h       SoftAP provisioning + captive portal + token config
├── time_sync.c/h       SNTP time sync with NVS timezone
├── price_fetch.c/h     Gate.io HTTP polling + candlestick history
├── token_config.c/h    Dynamic token registry and NVS persistence
├── homekit.c/h         Apple HomeKit (HAP) stateless programmable switch
├── led.c/h             WS2812B breathing + flash effects
├── boot_logo.c/h       Boot logo image data
└── crypto_logos.c/h    Embedded token logo image data
components/
├── esp-homekit-sdk/          Espressif HomeKit SDK (git submodule)
├── esp_lcd_jd9853/           JD9853 LCD driver (ESP32-S3)
└── esp_lcd_touch_axs5106/    AXS5106 touch driver (ESP32-S3)
sdkconfig.defaults            Shared build config
sdkconfig.defaults.esp32s3    ESP32-S3 overrides (16MB flash, PSRAM, 240MHz)
```

## Startup Sequence

```
app_main()
  ├── power_management_init()   DFS power management
  ├── display_init()            SPI bus, LCD panel, LVGL, touch (S3)
  ├── token_config_load()       Load selected tokens from NVS
  ├── led_init()                WS2812B breathing LED (skipped if no LED)
  ├── btn_init()                Button ISR + task (early for long-press provisioning)
  ├── wifi_init_sta()           Connect (NVS creds) or provision (SoftAP)
  │   if connected:
  │   ├── time_sync_init()      SNTP sync + timezone from NVS
  │   ├── price_fetch_first()   Synchronous first price fetch (all tokens)
  │   └── homekit_init()        Start HAP server + mDNS
  │   if timeout:
  │   └── background retry      Auto-reconnect every 10s
  ├── ui_init()                 Build crypto cards + info panel + gesture layer
  └── price_fetch_start()       Polling task (focused 10s / background 10min)
```

## Data Flow

```
Focused token (10s) ──┐
                      ├──> price_fetch ──> ui_update_price()
Background (10min) ───┘                       ├── Price labels + chart update
                                              ├── LED flash on change
                                              └── check_price_alert()
                                                   ├── Surge ≥ +5% → HomeKit SinglePress
                                                   └── Crash ≤ -5% → HomeKit DoublePress

Focus switch ──> 3s debounce ──> immediate fetch
```

## NVS Storage

| Namespace | Key | Value |
|-----------|-----|-------|
| wifi_cfg | ssid | Wi-Fi SSID (max 32 bytes) |
| wifi_cfg | pass | Wi-Fi password (max 64 bytes) |
| wifi_cfg | tz | POSIX timezone string (e.g. `CST-8`) |
| tok_cfg | tokens | Comma-separated token IDs (e.g. `bitcoin,ethereum,sui`) |
| hap_ctrl | — | HomeKit controller/pairing data |
| hap_main | — | HomeKit accessory info |

## Dependencies

- **ESP-IDF** v5.5+ (with FreeRTOS)
- [lvgl/lvgl](https://components.espressif.com/components/lvgl/lvgl) ~9.2
- [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) ^2
- [espressif/esp_lcd_touch](https://components.espressif.com/components/espressif/esp_lcd_touch) ^1
- [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) ^2
- [espressif/esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk) (git submodule)

## Roadmap

- [x] **Apple HomeKit** — Stateless Programmable Switch with price surge/crash alerts
- [x] **Dynamic Token Config** — Select tokens via the web portal
- [x] **ESP32-S3 Touch Support** — Touch gestures on Waveshare ESP32-S3-Touch-LCD-1.47
- [x] **GitHub Actions CI** — Automated builds and firmware releases
- [ ] **Stablecoin Yield Dashboard** — Display DeFi wallet balances, APY, and earnings
- [ ] **3D Printed Enclosure** — Design a desktop-friendly case

## Contributing

Contributions are welcome! Feel free to open an issue or submit a pull request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/my-feature`)
3. Commit your changes
4. Push to the branch (`git push origin feature/my-feature`)
5. Open a Pull Request

## Acknowledgments

- [Espressif ESP-IDF](https://github.com/espressif/esp-idf) — IoT development framework
- [LVGL](https://github.com/lvgl/lvgl) — Embedded graphics library
- [Waveshare](https://www.waveshare.com/) — Hardware platform
- [Gate.io](https://www.gate.io/) — Cryptocurrency market data API
- [esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk) — Apple HomeKit (HAP) for ESP32

## License

This project is licensed under [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) — free for personal and non-commercial use. Commercial use is not permitted without prior authorization.
