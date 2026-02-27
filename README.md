# TokenTicker

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/)
[![Platform](https://img.shields.io/badge/Platform-ESP32--C6-green?logo=espressif)](https://www.waveshare.com/esp32-c6-lcd-1.47.htm)
[![License](https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey)](LICENSE)
[![LVGL](https://img.shields.io/badge/LVGL-9.2-pink?logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMiIgaGVpZ2h0PSIzMiIgZmlsbD0ibm9uZSI+PGcgZmlsbD0iY3VycmVudENvbG9yIj48cGF0aCBmaWxsLXJ1bGU9ImV2ZW5vZGQiIGQ9Ik0zIDNhMyAzIDAgMCAwLTMgM3Y0YTEgMSAwIDAgMCAxIDFoNmEyIDIgMCAwIDEgMiAydjZhMSAxIDAgMCAwIDEgMWg2YTIgMiAwIDAgMSAyIDJ2NmExIDEgMCAwIDAgMSAxaDRhMyAzIDAgMCAwIDMtM1Y2YTMgMyAwIDAgMC0zLTN6bTE5LjUgNWExLjUgMS41IDAgMSAwIDAtMyAxLjUgMS41IDAgMCAwIDAgMyIgY2xpcC1ydWxlPSJldmVub2RkIi8+PC9nPjxyZWN0IHdpZHRoPSI4IiBoZWlnaHQ9IjgiIHk9IjEyIiBmaWxsPSIjZjM1ZjIwIiByeD0iMSIvPjxwYXRoIGZpbGw9IiM2MmQzNjciIGQ9Ik0wIDIyYTEgMSAwIDAgMSAxLTFoNmExIDEgMCAwIDEgMSAxdjZhMSAxIDAgMCAxLTEgMUgzYTMgMyAwIDAgMS0zLTN6Ii8+PHJlY3Qgd2lkdGg9IjgiIGhlaWdodD0iOCIgeD0iOSIgeT0iMjEiIGZpbGw9IiMzYzY1ZjgiIHJ4PSIxIi8+PC9zdmc+)](https://lvgl.io/)

[中文文档](README_zh.md)

An open-source crypto desktop companion powered by ESP32-C6. Real-time prices via WebSocket, trend charts, market mood LED, and phone-based Wi-Fi setup — all on a 1.47" LCD you can leave running 24/7.

Today it tracks **BTC**, **ETH**, and **PAXG** through Gate.io. Next up: custom token watchlists, DeFi yield dashboards, a 3D-printed enclosure, and Matter smart-home integration.

![WiFi Setup Screen](docs/images/wifi-setup.jpg)
![Boot Screen](docs/images/boosting.jpg)
![BTC Price View](docs/images/token-price1.jpg)
![PAXG Price View](docs/images/token-price2.jpg)
![System Info Panel](docs/images/system-info.jpg)

## Features

- **Real-Time Prices** — WebSocket push (~1s) for the focused coin, HTTP polling (10s) for side cards, animated price updates with 30-point history chart
- **Market Mood LED** — WS2812B breathing animation (green on gains, red on losses, 3.2s cycle), bright flash on price ticks
- **System Info Panel** — Chip temperature, heap usage, Wi-Fi signal strength, and clock (double-click to toggle)
- **SoftAP Provisioning** — No hardcoded Wi-Fi credentials; configure via captive portal from your phone
- **Timezone Config** — Auto-detected from browser during Wi-Fi setup, stored in NVS
- **WS Latency Display** — Real-time WebSocket latency shown on the main card

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

| Action | Behavior |
|--------|----------|
| Single click | Switch to next crypto (BTC → ETH → PAXG → BTC) |
| Double click | Toggle system info panel |
| Long press (3s) | Enter Wi-Fi provisioning mode |

## Wi-Fi Provisioning

On first boot (or long-press BOOT for 3 seconds):

1. Device creates open AP: **TokenTicker**
2. Connect from your phone — captive portal opens automatically
3. Select your Wi-Fi network from the scanned list
4. Enter password and choose timezone
5. Device saves to NVS and reboots

Credentials and timezone persist across power cycles. No need to recompile.

## UI Layout

### Crypto View (default)

```
┌──────────────────────────────────────────┐
│ ┌─────────────────────┐ ┌──────────────┐ │
│ │  BTC  Bitcoin       │ │  ETH         │ │
│ │  $68,432.10         │ │  $3,841.20   │ │
│ │  +2.34%             │ │  -0.82%      │ │
│ │  H: $69,100         │ ├──────────────┤ │
│ │  L: $67,200         │ │  C           │ │
│ │  ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃ │ │  $0.0312     │ │
│ └─────────────────────┘ └──────────────┘ │
└──────────────────────────────────────────┘
  Main card (200px)        Side cards (104px)
```

### Info Panel (double-click)

```
┌──────────────────────────────────────────┐
│ ┌──────┐ ┌──────────┐ ┌───────────────┐ │
│ │14:32 │ │ Temp  38°│ │ SSID: MyWiFi  │ │
│ │:07   │ │ Heap  22%│ │ IP: 10.0.1.42 │ │
│ │Thu   │ │          │ │ RSSI: -52 dBm │ │
│ │Feb 27│ │          │ │ Wi-Fi 4 2.4G  │ │
│ └──────┘ └──────────┘ └───────────────┘ │
└──────────────────────────────────────────┘
  Clock       System        Network
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
# Clone the repository
git clone https://github.com/f0rmatting/esp32-token-ticker.git
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
├── ui_info.c           System info panel (time, temp, heap, WiFi)
├── ui_internal.h       Shared UI state and layout constants
├── ui.h                Public UI interface
├── button.c            Button handler (single/double/long press)
├── wifi.c/h            Wi-Fi STA connection + auto-reconnect
├── wifi_prov.c/h       SoftAP provisioning + captive portal
├── time_sync.c/h       SNTP time sync with NVS timezone
├── price_fetch.c/h     Gate.io HTTP price polling
├── ws_price.c/h        Gate.io WebSocket real-time feed
├── led.c/h             WS2812B breathing + flash effects
└── crypto_logos.c/h    Embedded logo image data
sdkconfig.defaults      Build configuration presets
```

## Startup Sequence

```
app_main()
  ├── display_init()          SPI bus, ST7789 panel, backlight, LVGL
  ├── led_init()              WS2812B breathing LED
  ├── ui_boot_show()          Boot screen with progress bar
  ├── wifi_init_sta()         Connect (NVS creds) or provision (SoftAP)
  ├── time_sync_init()        SNTP sync + timezone from NVS
  ├── price_fetch_first()     Synchronous first price fetch
  ├── ui_boot_hide()
  ├── ui_init()               Build crypto cards + info panel
  ├── price_fetch_start()     HTTP polling (10s, side cards)
  └── ws_price_start(0)       WebSocket real-time (focused coin)
```

## Data Flow

```
WebSocket (~1s) ──> ws_price ──> ui_update_price_ws()
                                   ├── Real-time labels + scroll animation
                                   ├── Chart point every 10s
                                   ├── LED flash on chart update
                                   └── Latency display

HTTP (10s) ──> price_fetch ──> ui_update_price()
               (skips WS coin)    └── Side card labels + chart
```

## NVS Storage

| Namespace | Key | Value |
|-----------|-----|-------|
| wifi_cfg | ssid | Wi-Fi SSID (max 32 bytes) |
| wifi_cfg | pass | Wi-Fi password (max 64 bytes) |
| wifi_cfg | tz | POSIX timezone string (e.g. `CST-8`) |

## Dependencies

- **ESP-IDF** v5.x (with FreeRTOS)
- [lvgl/lvgl](https://components.espressif.com/components/lvgl/lvgl) ~9.2
- [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) ^2
- [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) ^2
- [espressif/esp_websocket_client](https://components.espressif.com/components/espressif/esp_websocket_client) ^1.2

## Roadmap

- [ ] **Custom Token Config** — Allow users to add/remove tokens via the web portal instead of hardcoded pairs
- [ ] **Stablecoin Yield Dashboard** — Display DeFi wallet balances, APY, and earnings for stablecoin strategies
- [ ] **3D Printed Enclosure** — Design a desktop-friendly case to turn the device into a proper desk gadget
- [ ] **Matter Support** — Integrate with Apple Home / Google Home / Mi Home for smart home automation (e.g. LED color as market indicator, trigger scenes on price alerts)

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

## License

This project is licensed under [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) — free for personal and non-commercial use. Commercial use is not permitted without prior authorization.
