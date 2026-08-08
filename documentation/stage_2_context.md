# RoboFusion Techathon - Stage 2 Documentation

## Overview
Stage 2 extends the Stage 1 baseline by integrating a DHT11 Temperature and Humidity sensor on GPIO 3 on an independent 5-second polling schedule while preserving all Stage 1 jobs intact.

## Hardware Configuration
- **Board:** ESP32-S3-CAM (`esp32-s3-devkitc-1`)
- **Indicator LED:** GPIO 1 (Output, 100 ms toggle)
- **IR Obstacle Sensor:** GPIO 2 (Input, 1000 ms poll + 3ms disconnect probe)
- **DHT11 Sensor:** GPIO 3 (Input/Data, 5000 ms poll)

## Key Technical Implementations

### 1. Multi-Task Scheduling
- Three concurrent jobs running on independent schedules:
  - Job 1: LED Heartbeat (100 ms)
  - Job 2: IR Sensor + Wire Probe (1000 ms)
  - Job 3: DHT11 Polling (5000 ms)
- Strictly non-blocking execution using `millis()`.

### 2. DHT11 Sensor Handling & Error Trapping
- Polled every 5000 ms (5 seconds).
- **Physical Disconnect Test:** Calls `isSensorConnected(DHT_PIN)` prior to reading. If data wire is missing, outputs `[ERROR] DHT11 Sensor Disconnected!`.
- **Read Error & Power-Loss Trapping:** Checks `isnan(hum) || isnan(temp) || (hum == 0.0 && temp == 0.0)`. Catches fake `0.0 °C / 0.0 %` readings caused by lost VCC/GND power while data wire remains plugged.
- **Valid Log Output:** Outputs `DHT11 -> Temp: XX.X °C, Humidity: XX.X %`.

### 3. Log Formatting
- All outputs contain `[%lu ms]` timestamp prefixes.
