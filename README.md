# ESP8266 GPS 码表项目

## 项目简介
本项目基于 ESP8266（如 ESP-12E）+ NEO-6M GPS 模块，实现了：
- 实时网页显示 GPS 数据和串口日志
- 码表功能（网页控制开始/结束，数据以 CSV 格式保存）
- LittleFS 持久化存储
- WiFi 配置（无 secrets.h 时自动进入 AP 模式，网页配置 WiFi）
- 支持 OLED（SSD1306）和 ST7735 两种屏幕，编译时可选
- AP 模式下支持 Captive Portal（DNS 劫持自动弹出 WiFi 配置页）
- 码表数据网页直接下载

## 主要功能
- 通过网页实时查看 GPS 信息、串口日志
- 网页可一键开始/结束码表，自动生成独立 CSV 文件
- LittleFS 存储 GPS 及码表数据，网页可直接下载
- 无 secrets.h 时自动进入 AP 模式，网页配置 WiFi
- 支持 OLED/彩屏，编译时通过 build_flags 选择
- AP 模式下 DNS 劫持，手机/电脑自动弹出配置页

## 使用说明
1. **硬件连接**
   - ESP8266（如 ESP-12E）
   - NEO-6M GPS 模块（默认 RX=D2, TX=D1）
   - 可选 OLED（SSD1306）或 ST7735 彩屏

2. **编译配置**
   - 使用 PlatformIO，推荐 VSCode 插件
   - `platformio.ini` 可通过 build_flags 选择屏幕类型：
     - `-D USE_OLED_SCREEN` 启用 OLED
     - `-D USE_ST7735_SCREEN` 启用 ST7735
   - 默认串口波特率 115200
   - `monitor_dtr = 0`、`monitor_rts = 0` 可避免打开串口时复位

3. **WiFi 配置**
   - 有 `src/secrets.h` 时自动连接指定 WiFi
   - 无 `secrets.h` 时自动进入 AP 模式，热点名 `GPS-Config`
   - 手机/电脑连接后自动弹出配置页，或访问 http://192.168.4.1

4. **网页功能**
   - 主页显示 GPS 实时数据、串口日志、码表控制按钮
   - 码表数据每秒自动记录，网页底部可直接下载所有历史 CSV 文件

5. **数据存储**
   - LittleFS 文件系统，GPS 日志和每次码表数据均独立保存
   - 码表数据标准 CSV 格式，便于后续分析

## 依赖库
- ESP8266WiFi
- ESP8266WebServer
- ESP8266HTTPClient
- mikalhart/TinyGPSPlus
- LittleFS
- Adafruit SSD1306
- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library

## 目录结构
```
platformio.ini         # 项目配置
src/main.cpp          # 主程序
src/secrets.h         # （可选）WiFi 密码头文件
lib/                  # 可选库
```

## 常见问题
- 打开串口监视器时自动复位/下载？
  - 可在 platformio.ini 设置 `monitor_dtr = 0`、`monitor_rts = 0`
- AP 模式下无法自动弹出配置页？
  - 确认手机/电脑 DNS 设置为自动获取，或手动访问 http://192.168.4.1

