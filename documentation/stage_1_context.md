# RoboFusion Techathon - Stage 1 Documentation

## Overview
Stage 1 establishes the baseline firmware architecture for the ESP32-S3-CAM board. It implements strict non-blocking multi-tasking to handle an indicator LED heartbeat alongside a dynamic IR obstacle sensor with physical disconnection detection.

## Hardware Configuration
- **Board:** ESP32-S3-CAM (`esp32-s3-devkitc-1`)
- **Indicator LED:** GPIO 1 (Output)
- **IR Obstacle Sensor:** GPIO 2 (Input)
- **Baud Rate:** 115200 (Upload & Monitor)

## Key Technical Implementations

### 1. Non-Blocking Concurrency
- Zero `delay()` calls inside `loop()`.
- Uses `millis()` tracking to manage task timing.

### 2. LED Heartbeat (Job 1)
- Toggles GPIO 1 state every 100 ms (5 Hz frequency).
- Acts as a hardware health indicator; runs continuously without stuttering.

### 3. IR Sensor & Wire Disconnect Probe (Job 2)
- Polled every 1000 ms (1 Hz).
- **Physical Disconnect Detection (`isSensorConnected`):**
  - Applies a 3000 µs (3 ms) settling delay to test GPIO 2 with internal pull-up and pull-down configurations.
  - 3 ms allows decoupling capacitors on unpowered sensor modules (VCC/GND removed) or floating pins (OUT wire removed) to settle.
  - If `pullupReading != pulldownReading` or voltage is floating, outputs `[ERROR] IR Sensor Disconnected!`.
  - Handles removal of OUT, VCC, or GND wires individually.
- **Obstacle Detection:**
  - Evaluated using `gpio_get_level()`.
  - `0` (LOW) -> `IR Sensor: Obstacle Detected! [LOW]`
  - `1` (HIGH) -> `IR Sensor: Clear (No Obstacle) [HIGH]`

### 4. Native USB Serial Logging
- Requires `platformio.ini` build flags `-D ARDUINO_USB_MODE=1` and `-D ARDUINO_USB_CDC_ON_BOOT=1` to route `Serial` output to the ESP32-S3 Native USB port.
- All log messages in `loop()` are prepended with `[%lu ms]` timestamps.
