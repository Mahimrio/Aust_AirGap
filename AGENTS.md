# RoboFusion Techathon - Agent Rules & Project Guidelines

This file serves as the single source of truth for AI agents (and human engineers) working on the RoboFusion ESP32-S3-CAM codebase.

---

## 1. System Architecture & Hardware Specs

- **Board Environment:** ESP32-S3-CAM (`esp32-s3-devkitc-1` in `platformio.ini`)
- **Framework:** Arduino (PlatformIO)
- **Pin Allocations:**
  - **GPIO 1:** Indicator LED (Output)
  - **GPIO 2:** IR Obstacle Sensor (Input)
  - **GPIO 3:** DHT11 Temperature & Humidity Sensor (Data)
- **Serial Configuration:**
  - Baud rate: `115200`
  - Native USB CDC configuration in `platformio.ini` MUST be preserved:
    ```ini
    build_flags = 
        -D ARDUINO_USB_MODE=1
        -D ARDUINO_USB_CDC_ON_BOOT=1
    ```

---

## 2. Core Execution Rules (DO'S)

1. **Strict Non-Blocking Concurrency:**
   - Always use `millis()` state timing logic in `loop()`.
   - Maintain independent schedules for each job:
     - LED Heartbeat: 100 ms (5 Hz)
     - IR Sensor: 1000 ms (1 Hz)
     - DHT11 Sensor: 5000 ms (0.2 Hz)
2. **Wire Disconnect Detection (`isSensorConnected`):**
   - Must use the 3000 µs (3 ms) settling delay in `isSensorConnected()` to test pull-up vs pull-down voltage differences.
   - This delay allows decoupling capacitors on unpowered sensor modules to charge, accurately flagging missing VCC, GND, or OUT/Signal wires.
3. **Sensor Reading Validation:**
   - IR Sensor: Use `gpio_get_level()` for digital LOW/HIGH evaluation.
   - DHT11 Sensor: Use `isnan(temp) || isnan(hum) || (hum == 0.0 && temp == 0.0)` to trap read timeouts and power-loss zeros.
4. **Log Formatting:**
   - Every log message printed in `loop()` MUST be prepended with a millisecond timestamp: `[%lu ms]`.

---

## 3. What NOT To Do (DON'TS)

- ❌ **NEVER use `delay()` in `loop()`:** Any blocking delay will freeze the LED heartbeat rhythm and violate competition rules.
- ❌ **NEVER remove USB build flags:** Removing `-D ARDUINO_USB_CDC_ON_BOOT=1` will break serial monitoring on the Native USB port.
- ❌ **NEVER raise `upload_speed` above `115200`:** High upload speeds cause serial packet noise/corruption (`0x89` error).
- ❌ **NEVER use `analogRead()` for IR sensor thresholding:** LM393 output saturation voltage (~0.28V) interferes with ADC thresholding. Use `gpio_get_level()` after `isSensorConnected()`.
- ❌ **NEVER output unvalidated dummy readings:** If a sensor fails or loses power, output `[ERROR]` instead of fake data.
