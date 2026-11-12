# 🛠️ Troubleshooting Guide

Encountered an issue? Check this guide for common solutions and debugging tips.

---

## ❓ Frequently Encountered Issues

### 1. Wi-Fi Connection Fails / Keeps Disconnecting
- **Cause**: Incorrect SSID/password or weak signal coverage.
- **Solution**: Double-check your credentials in the code. Ensure the ESP32-S3 is within range of a 2.4GHz Wi-Fi router (ESP32 does not support 5GHz networks).

### 2. Display Shows Blank Screen or Garbage/Garbled Text
- **Cause**: Incorrect pin definitions or missing font files in LittleFS.
- **Solution**: 
  - Verify your wiring against `HARDWARE.md`.
  - Ensure you have successfully uploaded the `data/` folder via the LittleFS Data Uploader tool.

---
*Got a new issue? Feel free to record your fix here for future reference!*