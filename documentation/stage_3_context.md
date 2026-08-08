# RoboFusion Techathon - Stage 3 Documentation

## Overview
Stage 3 implements dynamic WiFi provisioning, non-volatile memory (NVS via `Preferences.h`), an Access Point setup HTTP Web Server, an automatic 12-second fallback timeout, and a physical reset push-button on GPIO 45, all while preserving Stage 1 & Stage 2 baseline sensor jobs without blocking.

## Hardware Configuration
- **Board:** ESP32-S3-CAM (`esp32-s3-devkitc-1`)
- **Indicator LED:** GPIO 1 (Output, 100 ms toggle)
- **IR Obstacle Sensor:** GPIO 2 (Input, 1000 ms poll + 3ms disconnect probe)
- **DHT11 Sensor:** GPIO 3 (Input/Data, 5000 ms poll)
- **Physical Reset Push-Button:** GPIO 45 (Input with `INPUT_PULLUP`, held LOW > 2s to clear credentials)

## Key Technical Implementations

### 1. NVS Memory Storage (`Preferences.h`)
- **Namespace:** `"wifi_config"`
- **Keys:** `ssid`, `password`, `deviceName` (default: `"RoboFusion-ESP32"`)
- Loaded during `setup()`. If `ssid` is blank, immediately enters AP setup mode.

### 2. Access Point & HTTP Web Server Mode
- **AP SSID:** `RoboFusion-Setup` (Open Network, default IP: `192.168.4.1`)
- **Web Server Routes:**
  - `GET /`: Renders a modern, responsive HTML configuration form.
  - `POST /save`: Saves submitted `ssid`, `password`, and `deviceName` to NVS, sends a response page, and reboots the ESP32.

### 3. Non-Blocking 12-Second Fallback Timeout
- When booting with saved credentials, the ESP32 attempts connection in STA mode (`MODE_STA_CONNECTING`).
- If connection is not established within `12000 ms` (12 seconds), the system automatically logs a timeout and switches to AP Setup Mode (`startAPMode()`) without freezing the main loop.

### 4. Physical Reset Push-Button (GPIO 45)
- Polled continuously in `loop()`.
- If GPIO 45 is pulled LOW for > `2000 ms` (2 seconds), NVS preferences are cleared (`preferences.clear()`), `[RESET] Credentials Cleared!` is logged, and the board reboots into AP mode.

### 5. Task Concurrency Summary
- Job 1: LED Heartbeat (100 ms)
- Job 2: IR Sensor + Wire Probe (1000 ms)
- Job 3: DHT11 Sensor + Error Trap (5000 ms)
- Job 4: Reset Button Poll (GPIO 45)
- Job 5: WiFi State Machine & Web Server handling (`server.handleClient()`)
