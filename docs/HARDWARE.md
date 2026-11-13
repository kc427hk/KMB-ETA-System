[English](#english) | [繁體中文](#繁體中文)
---

## English

# 🔌 Hardware Requirements & Pinout

This document outlines the hardware components, specifications, and physical wiring requirements for the KMB ETA System based on using [Waveshare ESP32-S3 Touch LCD 3.5](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5).

---

## 🛠️ Components List
- **Microcontroller & Display**: [Waveshare ESP32-S3 Touch LCD 3.5](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5) (with official enclosure case) with 480x320 resolution.
- **Display Driver**: Driven via SPI using the `LovyanGFX` library
- **Power Supply**: 5V USB Type-C

> ⚠️ **Hardware Selection Note**: The Waveshare ESP32-S3 Touch LCD **3.5B** variant was intentionally avoided because its display uses a QSPI interface, which introduces uncertainties regarding full and stable driver support in `LovyanGFX`.

---

## 📌 Pinout Connections (Built-in SPI Display ST7796)
The display is integrated onboard and connected via SPI.

| ESP32-S3 Pin | Display Signal | Description |
| :--- | :--- | :--- |
| GPIO 01 | MOSI | Master Out Slave In (SPI Data) |
| GPIO 05 | SCLK | SPI Clock Line |
| *None / Hardwired* | CS | Chip Select |
| GPIO 03 | DC | Data / Command Control |
| *None / Hardwired* | RST | Hardware Reset |
| GPIO 06 | BL | Backlight Control (PWM dimming supported) |

---

## 📌 Pinout Connections (Built-in I2C & Peripherals)
The onboard power management chipset and touch controller(FT6336) share the I2C bus.

| ESP32-S3 Pin | Signal Name | Description |
| :--- | :--- | :--- |
| GPIO 07 | SCL | I2C Clock Line |
| GPIO 08 | SDA | I2C Data Line |
| GPIO 04 | EXIO_INT | External I/O Chip Interrupt Pin |

*(**Note**: These are built-in connections specific to the Waveshare ESP32-S3 Touch LCD 3.5 board layout.) Please refers to the full spec manual from Waveshare.*

---

## 繁體中文

# 🔌 硬件需求與引腳定義

本文件旨在說明基於 [Waveshare ESP32-S3 Touch LCD 3.5](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5) 開發板的 KMB ETA System 所需硬件組件、規格及實體接線要求。

---

## 🛠️ 組件清單
- **微控制器與顯示屏**：[Waveshare ESP32-S3 Touch LCD 3.5](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5)（連官方保護外殼），解像度為 480x320。
- **顯示驅動程式**：透過 SPI 介面並使用 `LovyanGFX` 程式庫驅動
- **電源供應**：5V USB Type-C

> ⚠️ **硬件選型備註**：本專案刻意不選用 Waveshare ESP32-S3 Touch LCD **3.5B** 型號，因為該版本的顯示屏採用 QSPI 介面，目前無法確定 `LovyanGFX` 驅動程式能否提供完整及穩定的支援。

---

## 📌 引腳連接（內置 SPI 顯示屏 ST7796）
顯示屏已內置於開發板上，並透過 SPI 介面連接。

| ESP32-S3 引腳 | 顯示屏訊號 | 功能說明 |
| :--- | :--- | :--- |
| GPIO 01 | MOSI | 主出從入（SPI 數據線） |
| GPIO 05 | SCLK | SPI 時脈訊號線 |
| *無／硬件直連* | CS | 晶片選擇（Chip Select） |
| GPIO 03 | DC | 數據／指令控制（Data / Command） |
| *無／硬件直連* | RST | 硬件重置（Hardware Reset） |
| GPIO 06 | BL | 背光亮度控制（支援 PWM 調光） |

---

## 📌 引腳連接（內置 I2C 與周邊元件）
開發板內置的電源管理晶片（PMIC）與觸控控制器（FT6336）共用同一組 I2C 匯流排。

| ESP32-S3 引腳 | 訊號名稱 | 功能說明 |
| :--- | :--- | :--- |
| GPIO 07 | SCL | I2C 時脈線 |
| GPIO 08 | SDA | I2C 數據線 |
| GPIO 04 | EXIO_INT | 外部 I/O 擴展晶片中斷引腳 |

*(**備註**：以上為 [Waveshare ESP32-S3 Touch LCD 3.5](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5) 開發板底板的內置預設連接。詳細資料請參閱 Waveshare 官方完整規格說明書。)*

