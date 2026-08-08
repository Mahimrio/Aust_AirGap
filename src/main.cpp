/*
 * ==========================================================
 *  RoboFusion 1.0 - Stage 2
 *  Board      : ESP32-S3-CAM
 *  Framework  : Arduino (PlatformIO)
 *
 *  Jobs (strictly non-blocking, millis()-based concurrency):
 *    1. LED Heartbeat : toggle GPIO1 every 100 ms (5 Hz)
 *    2. IR Polling    : read GPIO2 every 1000 ms (1 Hz)
 *    3. Disconnect    : dynamic PULLUP/PULLDOWN probe on GPIO2
 *    4. DHT11 Polling : read DHT11 on GPIO3 every 5000 ms (0.2 Hz)
 *
 *  No delay() is used inside loop(), so the LED rhythm keeps
 *  running even while the sensor is read or disconnected.
 * ==========================================================
 */

#include <Arduino.h>
#include <DHT.h>

/* ------------------------------------------------------------------
 * Pin Definitions & Objects
 * ------------------------------------------------------------------ */
static constexpr uint8_t LED_PIN = 1;  // GPIO1 - Indicator LED (Output)
static constexpr uint8_t IR_PIN  = 2;  // GPIO2 - IR Obstacle Sensor (Input)
static constexpr uint8_t DHT_PIN = 3;  // GPIO3 - DHT11 Data Pin

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

/* ------------------------------------------------------------------
 * Timing Configuration
 * ------------------------------------------------------------------ */
static constexpr uint32_t LED_TOGGLE_INTERVAL_MS = 100UL;  // 100 ms ON / 100 ms OFF -> 5 Hz
static constexpr uint32_t IR_POLL_INTERVAL_MS    = 1000UL; // Sensor polled every 1 second
static constexpr uint32_t DHT_POLL_INTERVAL_MS   = 5000UL; // Sensor polled every 5 seconds

/* ------------------------------------------------------------------
 * State Variables
 * ------------------------------------------------------------------ */
static uint32_t lastLedToggleMs = 0UL;
static uint32_t lastIrPollMs    = 0UL;
static uint32_t lastDhtPollMs   = 0UL;
static bool     ledState        = LOW;

/* ------------------------------------------------------------------
 * isSensorConnected()
 *
 * Detects if the jumper wire is physically attached to `pin`.
 *
 * Method:
 *   Enable the internal pull-up, sample the pin.
 *   Enable the internal pull-down, sample the pin.
 *   If the wire is connected to GND or VCC, the external signal
 *   dominates and BOTH samples read identically  -> CONNECTED.
 *   If the wire is removed (pin floating), the internal pull
 *   resistors dominate and the samples differ      -> DISCONNECTED.
 *
 * Uses ESP-IDF gpio_* functions for reliable pull-down on S3.
 *
 * Returns true  when the sensor is wired up,
 *         false when the pin is floating / disconnected.
 * ------------------------------------------------------------------ */
#include <driver/gpio.h>

static bool isSensorConnected(const uint8_t pin) {
  gpio_pad_select_gpio(static_cast<gpio_num_t>(pin));

  /* --- Pull-up sample ---
   * Allow 3000 us (3 ms) for any decoupling capacitors on unpowered sensor modules
   * (like when VCC is removed) to charge through the internal pull-up resistor. */
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);
  gpio_pullup_en(static_cast<gpio_num_t>(pin));
  gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
  ets_delay_us(3000);
  const int pullupReading = gpio_get_level(static_cast<gpio_num_t>(pin));

  /* --- Pull-down sample --- */
  gpio_pullup_dis(static_cast<gpio_num_t>(pin));
  gpio_pulldown_en(static_cast<gpio_num_t>(pin));
  ets_delay_us(3000);
  const int pulldownReading = gpio_get_level(static_cast<gpio_num_t>(pin));

  /* Restore to default input-pullup mode for normal polling. */
  gpio_pullup_en(static_cast<gpio_num_t>(pin));
  gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);

  return (pullupReading == pulldownReading);
}

/* ------------------------------------------------------------------
 * setup()
 * ------------------------------------------------------------------ */
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);

  /* Initialise IR pin once; probe will temporarily change mode. */
  gpio_reset_pin(static_cast<gpio_num_t>(IR_PIN));
  gpio_set_direction(static_cast<gpio_num_t>(IR_PIN), GPIO_MODE_INPUT);
  gpio_pullup_en(static_cast<gpio_num_t>(IR_PIN));

  /* Initialize DHT sensor */
  dht.begin();

  Serial.println();
  Serial.println("==========================================");
  Serial.println("   RoboFusion 1.0 - Stage 2 Initialized   ");
  Serial.println("==========================================");
}

/* ------------------------------------------------------------------
 * loop()  -- Non-blocking scheduler
 *
 * Both jobs run concurrently. Each job keeps its own `last*` stamp,
 * so one job can never delay or freeze the other.
 * ------------------------------------------------------------------ */
void loop() {
  const uint32_t now = millis();

  /* ---------------------------------------------------------------
   * Job 1 : LED Heartbeat (fixed 5 Hz rhythm, never blocks)
   * --------------------------------------------------------------- */
  if (now - lastLedToggleMs >= LED_TOGGLE_INTERVAL_MS) {
    lastLedToggleMs = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }

  /* ---------------------------------------------------------------
   * Job 2 : IR Sensor Poll + Disconnect Check (independent 1 s schedule)
   * --------------------------------------------------------------- */
  if (now - lastIrPollMs >= IR_POLL_INTERVAL_MS) {
    lastIrPollMs = now;

    if (!isSensorConnected(IR_PIN)) {
      Serial.printf("[%lu ms] [ERROR] IR Sensor Disconnected!\n", now);
    } else if (gpio_get_level(static_cast<gpio_num_t>(IR_PIN)) == 0) {
      Serial.printf("[%lu ms] IR Sensor: Obstacle Detected! [LOW]\n", now);
    } else {
      Serial.printf("[%lu ms] IR Sensor: Clear (No Obstacle) [HIGH]\n", now);
    }
  }

  /* ---------------------------------------------------------------
   * Job 3 : DHT11 Sensor Poll (independent 5 s schedule)
   * --------------------------------------------------------------- */
  if (now - lastDhtPollMs >= DHT_POLL_INTERVAL_MS) {
    lastDhtPollMs = now;

    // 1. Check if the wire is physically disconnected first
    if (!isSensorConnected(DHT_PIN)) {
      Serial.printf("[%lu ms] [ERROR] DHT11 Sensor Disconnected!\n", now);
    } else {
      // 2. If connected, attempt to read the values
      float hum = dht.readHumidity();
      float temp = dht.readTemperature();

      // 3. Validate the reading (DHT11 often returns 0.0/0.0 when power is lost but data wire remains)
      if (isnan(hum) || isnan(temp) || (hum == 0.0 && temp == 0.0)) {
        Serial.printf("[%lu ms] [ERROR] DHT11 Sensor Read Failed!\n", now);
      } else {
        Serial.printf("[%lu ms] DHT11 -> Temp: %.1f °C, Humidity: %.1f %%\n", now, temp, hum);
      }
    }
  }
}