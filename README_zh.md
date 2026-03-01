# TokenTicker

[![Build](https://github.com/f0rmatting/esp32-token-ticker/actions/workflows/build.yml/badge.svg)](https://github.com/f0rmatting/esp32-token-ticker/actions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3%20|%20ESP32--C6-green?logo=espressif)](https://www.waveshare.com/esp32-s3-touch-lcd-1.47.htm)
[![License](https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey)](LICENSE)
[![LVGL](https://img.shields.io/badge/LVGL-9.2-pink)](https://lvgl.io/)

[English](README.md)

基于 ESP32 的开源加密货币桌面伴侣。HTTP 实时价格轮询、24h K 线走势图、触摸手势、市场情绪呼吸灯、Apple HomeKit 智能家居联动、手机配网 — 1.47 寸 LCD，7x24 小时常亮运行。

支持多达 **10 种代币**（BTC、ETH、SOL、SUI、PAXG、BNB、XRP、DOGE、CBETH、USDT）— 通过配网页面自由配置。设备同时作为 Apple HomeKit 无状态可编程开关，价格暴涨暴跌可直接触发家庭自动化场景。

![WiFi 配网界面](docs/images/wifi-setup.jpg)
![启动画面](docs/images/boosting.jpg)
![BTC 价格界面](docs/images/token-price1.jpg)
![PAXG 价格界面](docs/images/token-price2.jpg)
![系统信息面板](docs/images/system-info.jpg)

## 功能特性

- **实时价格** — 聚焦币种 10s 轮询，后台币种 10min 轮询；价格滚动动画 + 48 点 24h K 线走势图（30 分钟间隔）
- **触摸手势**（ESP32-S3）— 左右滑动切换信息面板，上下滑动切换币种
- **动态 Token 配置** — 在配网页面从 10 种代币中选择 4–6 个关注币种
- **市场情绪呼吸灯** — WS2812B 涨绿跌红呼吸灯，价格变动时高亮闪烁（ESP32-C6）
- **Apple HomeKit** — 注册为无状态可编程开关；主币种 24h 涨幅 ≥+5% 触发单击事件，跌幅 ≤-5% 触发双击事件
- **系统信息面板** — 芯片温度弧形图、堆内存弧形图、Wi-Fi 信号强度柱状图、HomeKit 配对状态、时钟
- **SoftAP 配网** — 无需硬编码 Wi-Fi 凭据，手机连接热点自动弹出配网页面
- **启动动画** — Logo 玻璃光泽扫光效果

## 硬件

两款开发板共享同一 1.47 寸 LCD 规格，使用统一代码库支持。

| | ESP32-S3（触摸版） | ESP32-C6 |
|---|---|---|
| 开发板 | [Waveshare ESP32-S3-Touch-LCD-1.47](https://www.waveshare.com/esp32-s3-touch-lcd-1.47.htm) | [Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/esp32-c6-lcd-1.47.htm) |
| MCU | ESP32-S3 @ 240 MHz | ESP32-C6 @ 160 MHz |
| 显示屏 | 1.47 寸 JD9853 LCD, 320x172 | 1.47 寸 ST7789 LCD, 320x172 |
| 触摸 | AXS5106 电容触摸 (I2C) | — |
| Flash / PSRAM | 16 MB / 8 MB | 4 MB / — |
| LED | — | WS2812B RGB (1 颗) |
| 输入 | 触摸手势 + BOOT 按钮 | BOOT 按钮 |

## 操作方式

### 按钮（两款通用）

| 操作 | 价格界面 | 信息面板 |
|------|----------|----------|
| 单击 | 切换下一个币种 | 发送 HomeKit 单击事件 |
| 双击 | 切换信息面板 | 切换信息面板 |
| 长按 (3 秒) | 重置 HomeKit + 进入 Wi-Fi 配网 | 重置 HomeKit + 进入 Wi-Fi 配网 |

### 触摸手势（仅 ESP32-S3）

| 手势 | 动作 |
|------|------|
| 左滑 | 打开信息面板 |
| 右滑 | 返回价格界面 |
| 上滑 | 下一个币种 |
| 下滑 | 上一个币种 |

## Wi-Fi 配网

首次开机（或长按 BOOT 键 3 秒）：

1. 设备创建开放热点：**TokenTicker**
2. 手机连接后自动弹出 Captive Portal 配网页面
3. 从扫描列表中选择目标 Wi-Fi 网络
4. 输入密码、选择时区、选择关注的代币
5. 设备保存到 NVS 并自动重启

凭据、时区和代币配置断电不丢失，无需重新编译烧录。

## Apple HomeKit

设备注册为 Apple Home 中的**无状态可编程开关**：

- **配对码** — 根据设备 MAC 地址自动生成，在信息面板底部显示
- **配对方式** — 打开 Apple 家庭 → 添加配件 → 输入屏幕上显示的配对码
- **价格预警** — 主币种 24h 涨幅 ≥+5% 发送单击事件，跌幅 ≤-5% 发送双击事件
- **自动化** — 利用这些事件触发家庭场景（需要家庭中枢：HomePod、Apple TV 或 iPad）

> 这行情，在家开灯都觉得是在烧钱。所以接了 HomeKit — BTC 暴跌自动关灯省电，每一度都不能浪费。

长按重置会同时清除 Wi-Fi 凭据和 HomeKit 配对数据。

## 界面布局

### 价格界面（默认）

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
  主卡片 (上下滑动切换)    侧边卡片 (滚动)
```

### 信息面板（左滑 / 双击）

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

## 快速开始（预编译固件）

无需搭建开发环境，浏览器直接刷入：

1. 从 [Releases](https://github.com/f0rmatting/esp32-token-ticker/releases) 下载最新固件
2. 用 USB-C 连接开发板
3. 用 Chrome/Edge 打开 [ESP Web Flasher](https://espressif.github.io/esptool-js/)
4. 点击 **Connect**，选择串口
5. 刷入 **factory bin**（单文件，已包含全部内容）：

| 地址 | 文件 |
|------|------|
| `0x0` | `token_ticker-esp32s3-factory.bin` 或 `token_ticker-esp32c6-factory.bin` |

6. 点击 **Program**，等待完成
7. 首次开机自动进入 Wi-Fi 配网模式

## 从源码编译

### 前置条件

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32s3/get-started/) v5.5+
- 一块支持的 Waveshare 开发板（ESP32-S3-Touch-LCD-1.47 或 ESP32-C6-LCD-1.47）
- USB-C 数据线

### 本地编译

```bash
# 克隆仓库（--recursive 拉取 esp-homekit-sdk 子模块）
git clone --recursive https://github.com/f0rmatting/esp32-token-ticker.git
cd esp32-token-ticker

# 编译 ESP32-S3
idf.py set-target esp32s3
idf.py build

# 或编译 ESP32-C6
idf.py set-target esp32c6
idf.py build

# 烧录并监控
idf.py -p <PORT> flash monitor
```

### Docker 编译（无需本地安装 ESP-IDF）

```bash
docker run --rm -v $PWD:/project -w /project espressif/idf:v5.5.3 \
  bash -c ". \$IDF_PATH/export.sh && idf.py set-target esp32s3 && idf.py build"
```

## 为什么选用 Gate.io？

作为全球领先的头部交易所，Gate.io 提供了一个显著的技术优势：其 API 在中国大陆无需 VPN 即可直连。对于像本项目这样需要 7x24 小时稳定、实时获取行情数据的嵌入式设备，Gate.io 是主流平台中确保高可用数据采集最务实的选择。

## 项目结构

```
main/
├── token_ticker.c      入口 (app_main)
├── board_config.h      多目标 GPIO 引脚定义 (C6 / S3)
├── display.c/h         SPI + LCD 面板 + 触摸 + LVGL 初始化
├── ui.c                主界面、启动画面、价格卡片、触摸手势
├── ui_info.c           信息面板 (时钟、温度弧形、内存弧形、WiFi、HomeKit)
├── ui_internal.h       UI 模块共享状态和布局常量
├── ui.h                UI 公共接口
├── button.c            按钮处理 (单击/双击/长按)
├── wifi.c/h            Wi-Fi STA 连接 + 自动重连
├── wifi_prov.c/h       SoftAP 配网 + Captive Portal + 代币配置
├── time_sync.c/h       SNTP 时间同步 + NVS 时区
├── price_fetch.c/h     Gate.io HTTP 轮询 + K 线历史
├── token_config.c/h    动态代币注册表 + NVS 持久化
├── homekit.c/h         Apple HomeKit (HAP) 无状态可编程开关
├── led.c/h             WS2812B 呼吸灯 + 闪烁效果
├── boot_logo.c/h       启动 Logo 图片数据
└── crypto_logos.c/h    内嵌代币 Logo 图片数据
components/
├── esp-homekit-sdk/          Espressif HomeKit SDK (git 子模块)
├── esp_lcd_jd9853/           JD9853 LCD 驱动 (ESP32-S3)
└── esp_lcd_touch_axs5106/    AXS5106 触摸驱动 (ESP32-S3)
sdkconfig.defaults            共享编译配置
sdkconfig.defaults.esp32s3    ESP32-S3 覆盖配置 (16MB Flash, PSRAM, 240MHz)
```

## 启动流程

```
app_main()
  ├── power_management_init()   DFS 电源管理
  ├── display_init()            SPI 总线、LCD 面板、LVGL、触摸 (S3)
  ├── token_config_load()       从 NVS 加载代币配置
  ├── led_init()                WS2812B 呼吸灯（无 LED 硬件则跳过）
  ├── btn_init()                按钮中断 + 任务（提前初始化，配网长按可用）
  ├── wifi_init_sta()           连接 (NVS 凭据) 或配网 (SoftAP)
  │   如果连接成功:
  │   ├── time_sync_init()      SNTP 同步 + 从 NVS 读取时区
  │   ├── price_fetch_first()   同步首次价格拉取（全部代币）
  │   └── homekit_init()        启动 HAP 服务器 + mDNS
  │   如果超时:
  │   └── 后台重连              每 10s 自动重试
  ├── ui_init()                 构建价格卡片 + 信息面板 + 手势图层
  └── price_fetch_start()       轮询任务（聚焦 10s / 后台 10min）
```

## 数据流

```
聚焦币种 (10s) ────┐
                   ├──> price_fetch ──> ui_update_price()
后台币种 (10min) ──┘                       ├── 价格标签 + 图表更新
                                           ├── LED 闪烁
                                           └── check_price_alert()
                                                ├── 涨幅 ≥ +5% → HomeKit 单击事件
                                                └── 跌幅 ≤ -5% → HomeKit 双击事件

切换币种 ──> 3s 防抖 ──> 立即拉取
```

## NVS 存储

| 命名空间 | 键 | 值 |
|----------|-----|-----|
| wifi_cfg | ssid | Wi-Fi SSID (最长 32 字节) |
| wifi_cfg | pass | Wi-Fi 密码 (最长 64 字节) |
| wifi_cfg | tz | POSIX 时区字符串 (如 `CST-8`) |
| tok_cfg | tokens | 逗号分隔的代币 ID (如 `bitcoin,ethereum,sui`) |
| hap_ctrl | — | HomeKit 控制器/配对数据 |
| hap_main | — | HomeKit 配件信息 |

## 依赖

- **ESP-IDF** v5.5+ (含 FreeRTOS)
- [lvgl/lvgl](https://components.espressif.com/components/lvgl/lvgl) ~9.2
- [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) ^2
- [espressif/esp_lcd_touch](https://components.espressif.com/components/espressif/esp_lcd_touch) ^1
- [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) ^2
- [espressif/esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk) (git 子模块)

## 路线图

- [x] **Apple HomeKit** — 无状态可编程开关 + 价格暴涨暴跌预警
- [x] **动态 Token 配置** — 通过配网页面自由选择关注的币种
- [x] **ESP32-S3 触摸支持** — Waveshare ESP32-S3-Touch-LCD-1.47 触摸手势
- [x] **GitHub Actions CI** — 自动构建和固件发布
- [ ] **稳定币理财看板** — 展示 DeFi 钱包余额、年化收益率和收益明细
- [ ] **3D 打印外壳** — 设计桌面摆件外壳

## 参与贡献

欢迎贡献代码！可以通过 Issue 反馈问题或提交 Pull Request。

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/my-feature`)
3. 提交更改
4. 推送到分支 (`git push origin feature/my-feature`)
5. 发起 Pull Request

## 致谢

- [Espressif ESP-IDF](https://github.com/espressif/esp-idf) — IoT 开发框架
- [LVGL](https://github.com/lvgl/lvgl) — 嵌入式图形库
- [Waveshare](https://www.waveshare.com/) — 硬件平台
- [Gate.io](https://www.gate.io/) — 加密货币行情数据 API
- [esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk) — ESP32 Apple HomeKit (HAP) SDK

## 许可证

本项目采用 [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/deed.zh-hans) 许可证 — 允许个人和非商业用途自由使用。未经授权不得用于商业用途。
