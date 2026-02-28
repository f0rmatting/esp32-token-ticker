# TokenTicker

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/)
[![Platform](https://img.shields.io/badge/Platform-ESP32--C6-green?logo=espressif)](https://www.waveshare.com/esp32-c6-lcd-1.47.htm)
[![License](https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey)](LICENSE)
[![LVGL](https://img.shields.io/badge/LVGL-9.2-pink?logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMiIgaGVpZ2h0PSIzMiIgZmlsbD0ibm9uZSI+PGcgZmlsbD0iY3VycmVudENvbG9yIj48cGF0aCBmaWxsLXJ1bGU9ImV2ZW5vZGQiIGQ9Ik0zIDNhMyAzIDAgMCAwLTMgM3Y0YTEgMSAwIDAgMCAxIDFoNmEyIDIgMCAwIDEgMiAydjZhMSAxIDAgMCAwIDEgMWg2YTIgMiAwIDAgMSAyIDJ2NmExIDEgMCAwIDAgMSAxaDRhMyAzIDAgMCAwIDMtM1Y2YTMgMyAwIDAgMC0zLTN6bTE5LjUgNWExLjUgMS41IDAgMSAwIDAtMyAxLjUgMS41IDAgMCAwIDAgMyIgY2xpcC1ydWxlPSJldmVub2RkIi8+PC9nPjxyZWN0IHdpZHRoPSI4IiBoZWlnaHQ9IjgiIHk9IjEyIiBmaWxsPSIjZjM1ZjIwIiByeD0iMSIvPjxwYXRoIGZpbGw9IiM2MmQzNjciIGQ9Ik0wIDIyYTEgMSAwIDAgMSAxLTFoNmExIDEgMCAwIDEgMSAxdjZhMSAxIDAgMCAxLTEgMUgzYTMgMyAwIDAgMS0zLTN6Ii8+PHJlY3Qgd2lkdGg9IjgiIGhlaWdodD0iOCIgeD0iOSIgeT0iMjEiIGZpbGw9IiMzYzY1ZjgiIHJ4PSIxIi8+PC9zdmc+)](https://lvgl.io/)

[中文文档](README_zh.md)

An open-source crypto desktop companion powered by ESP32-C6. Real-time prices via HTTP polling, 24h candlestick charts, market mood LED, Apple HomeKit integration, and phone-based Wi-Fi setup — all on a 1.47" LCD you can leave running 24/7.

Tracks **BTC**, **ETH**, **PAXG**, and **SUI** through Gate.io. The device also acts as an Apple HomeKit Stateless Programmable Switch — price surge/crash alerts can trigger Home automations directly.

![WiFi Setup Screen](docs/images/wifi-setup.jpg)
![Boot Screen](docs/images/boosting.jpg)
![BTC Price View](docs/images/token-price1.jpg)
![PAXG Price View](docs/images/token-price2.jpg)
![System Info Panel](docs/images/system-info.jpg)

## Features

- **Real-Time Prices** — HTTP polling (10s) for all 4 coins, animated price updates with 48-point 24h candlestick chart (30min intervals)
- **Market Mood LED** — WS2812B breathing animation (green on gains, red on losses, 3.2s cycle), bright flash on price ticks
- **Apple HomeKit** — Appears as a Stateless Programmable Switch in Apple Home; price surge (+5%) triggers SinglePress, crash (-5%) triggers DoublePress for automations
- **System Info Panel** — Chip temperature arc, heap usage arc, Wi-Fi signal bars, HomeKit pairing status, and clock (double-click to toggle)
- **SoftAP Provisioning** — No hardcoded Wi-Fi credentials; configure via captive portal from your phone
- **Timezone Config** — Auto-detected from browser during Wi-Fi setup, stored in NVS

## Hardware

| Component | Spec |
|-----------|------|
| Board | [Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/esp32-c6-lcd-1.47.htm) |
| MCU | ESP32-C6 |
| Display | 1.47" ST7789 LCD, 320x172, SPI @ 80 MHz |
| LED | WS2812B RGB (1 pixel) |
| Button | BOOT button (GPIO9, active low) |

### GPIO Pinout

| GPIO | Function |
|------|----------|
| 6 | SPI MOSI |
| 7 | SPI CLK |
| 14 | LCD CS |
| 15 | LCD DC |
| 21 | LCD RST |
| 22 | LCD Backlight (PWM) |
| 8 | WS2812B LED |
| 9 | BOOT Button |
| 4 | SD Card CS (held high) |

## Button Controls

| Action | Crypto View | Info Panel |
|--------|-------------|------------|
| Single click | Switch to next coin (BTC → ETH → PAXG → SUI) | Send HomeKit SinglePress event |
| Double click | Toggle info panel | Toggle info panel |
| Long press (3s) | Reset HomeKit + enter Wi-Fi provisioning | Reset HomeKit + enter Wi-Fi provisioning |

## Wi-Fi Provisioning

On first boot (or long-press BOOT for 3 seconds):

1. Device creates open AP: **TokenTicker**
2. Connect from your phone — captive portal opens automatically
3. Select your Wi-Fi network from the scanned list
4. Enter password and choose timezone
5. Device saves to NVS and reboots

Credentials and timezone persist across power cycles. No need to recompile.

## Apple HomeKit

The device registers as a **Stateless Programmable Switch** in Apple Home:

- **Setup code** — Derived from your device's MAC address, displayed on the Info panel
- **Pairing** — Open Apple Home → Add Accessory → enter the setup code shown on screen
- **Price alerts** — When the focused coin surges +5% (24h), a SinglePress event is sent; when it crashes -5%, a DoublePress event is sent
- **Automations** — Use these events to trigger Apple Home scenes (requires a Home Hub: HomePod, Apple TV, or iPad)

> With the market like this, even turning on the lights at home feels like burning money. So we hooked it up to HomeKit — when BTC crashes, the lights automatically turn off to save on electricity. Every little bit helps. 🐶

Long-press reset clears both Wi-Fi credentials and HomeKit pairing data.

## UI Layout

### Crypto View (default)

```
┌──────────────────────────────────────────┐
│ ┌─────────────────────┐ ┌──────────────┐ │
│ │  BTC  Bitcoin       │ │  ETH         │ │
│ │  $68,432.10         │ │  $3,841.20   │ │
│ │  +2.34%             │ │  -0.82%      │ │
│ │  H: $69,100         │ ├──────────────┤ │
│ │  L: $67,200         │ │  SUI         │ │
│ │  ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃ │ │  $1.23       │ │
│ └─────────────────────┘ └──────────────┘ │
└──────────────────────────────────────────┘
  Main card                 Side cards (marquee)
```

### Info Panel (double-click)

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
5. Add the following files at the correct addresses:

| Address | File |
|---------|------|
| `0x0` | `bootloader.bin` |
| `0x8000` | `partition-table.bin` |
| `0x10000` | `token_ticker.bin` |

6. Click **Program** and wait for completion
7. On first boot the device enters Wi-Fi provisioning mode automatically

## Build from Source

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/) v5.x
- A [Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/esp32-c6-lcd-1.47.htm) board
- USB-C cable

```bash
# Clone the repository (--recursive for esp-homekit-sdk submodule)
git clone --recursive https://github.com/f0rmatting/esp32-token-ticker.git
cd esp32-token-ticker

# Build
idf.py set-target esp32c6
idf.py build

# Flash and monitor
idf.py -p <PORT> flash monitor
```

On first boot the device will automatically enter Wi-Fi provisioning mode.

## Why Gate.io?

As a top-tier global exchange, Gate.io provides a significant technical advantage: its API is directly accessible from mainland China without the need for a VPN. For an always-on embedded device like this ESP32-C6 monitor that demands reliable, uninterrupted price data, Gate.io is the most practical and robust choice among leading platforms.

## Project Structure

```
main/
├── token_ticker.c      Entry point (app_main)
├── board_config.h      GPIO pin definitions
├── display.c/h         SPI + ST7789 + LVGL initialization
├── ui.c                Main crypto UI, boot screen, price cards
├── ui_info.c           Info panel (time, temp arc, heap arc, WiFi, HomeKit)
├── ui_internal.h       Shared UI state and layout constants
├── ui.h                Public UI interface
├── button.c            Button handler (single/double/long press)
├── wifi.c/h            Wi-Fi STA connection + auto-reconnect
├── wifi_prov.c/h       SoftAP provisioning + captive portal
├── time_sync.c/h       SNTP time sync with NVS timezone
├── price_fetch.c/h     Gate.io HTTP price polling + candlestick history
├── homekit.c/h         Apple HomeKit (HAP) stateless programmable switch
├── led.c/h             WS2812B breathing + flash effects
└── crypto_logos.c/h    Embedded logo image data
components/
└── esp-homekit-sdk/    Espressif HomeKit SDK (git submodule)
sdkconfig.defaults      Build configuration presets
```

## Startup Sequence

```
app_main()
  ├── power_management_init()   DFS power management
  ├── display_init()            SPI bus, ST7789 panel, backlight, LVGL
  ├── led_init()                WS2812B breathing LED
  ├── btn_init()                Button ISR + task (early for long-press provisioning)
  ├── wifi_init_sta()           Connect (NVS creds) or provision (SoftAP)
  │   if connected:
  │   ├── time_sync_init()      SNTP sync + timezone from NVS
  │   ├── price_fetch_history() Pre-fill 24h candlestick charts
  │   ├── price_fetch_first()   Synchronous first price fetch (all 4 coins)
  │   └── homekit_init()        Start HAP server + mDNS
  ├── ui_init()                 Build crypto cards + info panel
  └── price_fetch_start()       HTTP polling task (10s cycle)
```

## Data Flow

```
HTTP (10s) ──> price_fetch ──> ui_update_price()
                                  ├── Price labels + chart update
                                  ├── LED flash on change
                                  └── check_price_alert()
                                       ├── Surge ≥ +5% → HomeKit SinglePress
                                       └── Crash ≤ -5% → HomeKit DoublePress
```

## NVS Storage

| Namespace | Key | Value |
|-----------|-----|-------|
| wifi_cfg | ssid | Wi-Fi SSID (max 32 bytes) |
| wifi_cfg | pass | Wi-Fi password (max 64 bytes) |
| wifi_cfg | tz | POSIX timezone string (e.g. `CST-8`) |
| hap_ctrl | — | HomeKit controller/pairing data |
| hap_main | — | HomeKit accessory info |

## Dependencies

- **ESP-IDF** v5.x (with FreeRTOS)
- [lvgl/lvgl](https://components.espressif.com/components/lvgl/lvgl) ~9.2
- [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) ^2
- [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) ^2
- [espressif/esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk) (git submodule)

## Roadmap

- [x] **Apple HomeKit** — Stateless Programmable Switch with price surge/crash alerts
- [ ] **Custom Token Config** — Allow users to add/remove tokens via the web portal instead of hardcoded pairs
- [ ] **Stablecoin Yield Dashboard** — Display DeFi wallet balances, APY, and earnings for stablecoin strategies
- [ ] **3D Printed Enclosure** — Design a desktop-friendly case to turn the device into a proper desk gadget

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
