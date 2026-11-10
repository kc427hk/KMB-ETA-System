# 🚌 KMB ETA System (ESP32-S3)

[![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3-blue.svg)](https://www.espressif.com/)
[![LovyanGFX](https://img.shields.io/badge/Display-LovyanGFX-orange.svg)](https://github.com/lovyan03/LovyanGFX)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A high-performance, real-time KMB (Kowloon Motor Bus) estimated time of arrival (ETA) display system powered by **ESP32-S3** and **LovyanGFX**, custom-tailored for City One Shatin commuters.

Credited by: **KC427**

---

## 🌟 Features

- **Real-Time ETA Fetching**: Connects via Wi-Fi to fetch live bus arrival countdowns and schedules.
- **High-Performance Rendering**: Utilizes `LovyanGFX` for smooth, tearing-free graphics and responsive UI updates.
- **Custom Chinese Character & Font Support**: Embedded `.vlw` smooth fonts loaded via LittleFS for clear local language rendering.
- **Compact & Dedicated Hardware**: Designed specifically for low-power, dedicated desktop/wall-mounted display setups.

---

## 🛠️ Hardware Requirements

* **Microcontroller**: ESP32-S3 development board.
* **Display**: Compatible SPI/RGB LCD display driven by `LovyanGFX`.
* **Storage**: LittleFS partition for storing system assets and font files.

---

## 📂 Project Structure

```text
KMB-ETA-System/
│
├── BusTimes_FromCityone_v2.ino  # Main application logic & Wi-Fi / API handling
├── icons.h                    # Custom UI icons and graphical assets
├── data/                      # LittleFS data folder (contains .vlw fonts)
└── .gitignore                 # Git ignore rules for build artifacts & temp files