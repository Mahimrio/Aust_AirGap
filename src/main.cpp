/*
 * ==========================================================
 *  RoboFusion 1.0 - Stage 3 & 4 Ready (WiFi Setup & Dashboard)
 *  Board      : ESP32-S3-CAM
 *  Framework  : Arduino (PlatformIO)
 *
 *  Jobs (strictly non-blocking, millis()-based concurrency):
 *    1. LED Heartbeat   : toggle GPIO1 every 100 ms (5 Hz)
 *    2. IR Polling      : read GPIO2 every 1000 ms (1 Hz) + 3ms probe
 *    3. DHT11 Polling   : read DHT11 on GPIO3 every 5000 ms (0.2 Hz)
 *    4. Button Polling  : poll GPIO45 for >2s hold to reset NVS
 *    5. WiFi / WebServer: handle STA connect timeout (12s), AP server, and Dashboard
 * ==========================================================
 */

#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <driver/gpio.h>

/* ------------------------------------------------------------------
 * Pin Definitions & Objects
 * ------------------------------------------------------------------ */
static constexpr uint8_t LED_PIN    = 1;   // GPIO1 - Indicator LED (Output)
static constexpr uint8_t IR_PIN     = 2;   // GPIO2 - IR Obstacle Sensor (Input)
static constexpr uint8_t DHT_PIN    = 3;   // GPIO3 - DHT11 Data Pin
static constexpr uint8_t BUTTON_PIN = 45;  // GPIO45 - Physical Reset Push-Button (Input Pullup)

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

/* ------------------------------------------------------------------
 * System Modes & Timing Configuration
 * ------------------------------------------------------------------ */
enum SystemMode {
  MODE_STA_CONNECTING,
  MODE_STA_CONNECTED,
  MODE_AP_SETUP
};

static SystemMode currentMode = MODE_STA_CONNECTING;

static constexpr uint32_t LED_TOGGLE_INTERVAL_MS = 100UL;   // 5 Hz rhythm
static constexpr uint32_t IR_POLL_INTERVAL_MS    = 1000UL;  // 1 s schedule
static constexpr uint32_t DHT_POLL_INTERVAL_MS   = 5000UL;  // 5 s schedule
static constexpr uint32_t WIFI_TIMEOUT_MS        = 15000UL; // 15 s connection fallback timeout
static constexpr uint32_t BUTTON_HOLD_TIME_MS    = 2000UL;  // 2 s reset hold time

/* ------------------------------------------------------------------
 * State Variables
 * ------------------------------------------------------------------ */
static uint32_t lastLedToggleMs   = 0UL;
static uint32_t lastIrPollMs      = 0UL;
static uint32_t lastDhtPollMs     = 0UL;
static uint32_t wifiConnectStartMs= 0UL;
static uint32_t buttonPressStartMs= 0UL;
static uint32_t rebootScheduledMs = 0UL;

static bool ledState             = LOW;
static bool buttonPressedState   = false;
static bool pendingReboot        = false;

static String wifiSSID        = "";
static String wifiPassword    = "";
static String deviceName      = "RoboFusion-AirGap";

/* ------------------------------------------------------------------
 * isSensorConnected()
 * ------------------------------------------------------------------ */
static bool isSensorConnected(const uint8_t pin) {
  gpio_pad_select_gpio(static_cast<gpio_num_t>(pin));

  /* --- Pull-up sample --- */
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
 * Web Server Route Handlers
 * ------------------------------------------------------------------ */
static void handleDashboard() {
  String html = "<!DOCTYPE html><html><head><title>RoboFusion Dashboard</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; background: #0f172a; color: #f8fafc; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }";
  html += ".card { background: rgba(30, 41, 59, 0.8); backdrop-filter: blur(10px); padding: 2.5rem; border-radius: 16px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); width: 100%; max-width: 420px; border: 1px solid #334155; text-align: center; }";
  html += "h2 { color: #38bdf8; margin-bottom: 1.5rem; font-size: 1.5rem; }";
  html += ".badge { display: inline-block; padding: 0.35rem 0.85rem; background: #10b981; color: #fff; border-radius: 20px; font-size: 0.875rem; font-weight: bold; margin-bottom: 1.5rem; }";
  html += ".info-box { background: #1e293b; padding: 1rem; border-radius: 8px; margin-bottom: 1rem; border: 1px solid #475569; text-align: left; }";
  html += ".info-box strong { color: #94a3b8; font-size: 0.85rem; display: block; margin-bottom: 0.25rem; }";
  html += ".info-box span { color: #fff; font-size: 1.1rem; font-weight: bold; }";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<h2>RoboFusion Dashboard</h2>";
  html += "<div class='badge'>Connected & Online</div>";
  html += "<div class='info-box'><strong>Device Name</strong><span>" + deviceName + "</span></div>";
  html += "<div class='info-box'><strong>Connected Network</strong><span>" + wifiSSID + "</span></div>";
  html += "<div class='info-box'><strong>IP Address</strong><span>" + WiFi.localIP().toString() + "</span></div>";
  html += "<div class='info-box'><strong>System Status</strong><span>Online (Non-blocking Scheduler Active)</span></div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

static void handleSetupForm() {
  String html = "<!DOCTYPE html><html><head><title>RoboFusion Setup</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; background: #0f172a; color: #f8fafc; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }";
  html += ".card { background: rgba(30, 41, 59, 0.8); backdrop-filter: blur(10px); padding: 2.5rem; border-radius: 16px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); width: 100%; max-width: 380px; border: 1px solid #334155; }";
  html += "h2 { color: #38bdf8; text-align: center; margin-bottom: 1.5rem; font-size: 1.5rem; }";
  html += "label { display: block; margin-bottom: 0.5rem; font-size: 0.875rem; color: #94a3b8; }";
  html += "input[type='text'], input[type='password'] { width: 100%; padding: 0.75rem; margin-bottom: 1.25rem; background: #1e293b; border: 1px solid #475569; border-radius: 8px; color: #fff; box-sizing: border-box; }";
  html += "input:focus { outline: none; border-color: #38bdf8; }";
  html += "button { width: 100%; padding: 0.875rem; background: #0284c7; border: none; border-radius: 8px; color: #fff; font-weight: bold; font-size: 1rem; cursor: pointer; transition: background 0.2s; }";
  html += "button:hover { background: #0369a1; }";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<h2>RoboFusion Setup</h2>";
  html += "<form action='/save' method='POST'>";
  html += "<label>WiFi SSID</label><input type='text' name='ssid' value='" + wifiSSID + "' required placeholder='Enter WiFi Name'>";
  html += "<label>Password</label><input type='password' name='password' placeholder='Enter WiFi Password'>";
  html += "<label>Device Name</label><input type='text' name='deviceName' value='" + deviceName + "' required>";
  html += "<button type='submit'>Save & Connect</button>";
  html += "</form></div></body></html>";

  server.send(200, "text/html", html);
}

static void handleRoot() {
  if (currentMode == MODE_STA_CONNECTED) {
    handleDashboard();
  } else {
    handleSetupForm();
  }
}

static void handleSave() {
  if (server.hasArg("ssid")) {
    wifiSSID     = server.arg("ssid");
    wifiPassword = server.arg("password");
    if (server.hasArg("deviceName") && server.arg("deviceName").length() > 0) {
      deviceName = server.arg("deviceName");
    }

    preferences.begin("wifi_config", false);
    preferences.putString("ssid", wifiSSID);
    preferences.putString("password", wifiPassword);
    preferences.putString("deviceName", deviceName);
    preferences.end();

    String resp = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    resp += "<style>body{font-family:Arial;background:#0f172a;color:#38bdf8;text-align:center;padding-top:20vh;}</style></head>";
    resp += "<body><h2>Credentials Saved!</h2><p>Rebooting ESP32-S3 to connect to '" + wifiSSID + "'...</p></body></html>";
    
    server.send(200, "text/html", resp);

    Serial.printf("[%lu ms] [NVS] Credentials saved for '%s'. Rebooting in 1s...\n", millis(), wifiSSID.c_str());
    
    /* Schedule non-blocking reboot */
    pendingReboot = true;
    rebootScheduledMs = millis() + 1000UL;
  } else {
    server.send(400, "text/plain", "Missing SSID");
  }
}

static void initWebServerRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([]() {
    server.send(204, "text/plain", "");
  });
  server.begin();
}

/* ------------------------------------------------------------------
 * startAPMode()
 * ------------------------------------------------------------------ */
static void startAPMode() {
  WiFi.mode(WIFI_AP);
  /* Open Access Point network for easy setup portal access */
  WiFi.softAP("RoboFusion-AirGap");

  /* Start Captive Portal DNS Server (redirects all domains to 192.168.4.1) */
  dnsServer.start(53, "*", WiFi.softAPIP());

  initWebServerRoutes();

  currentMode = MODE_AP_SETUP;
  Serial.printf("[%lu ms] AP Mode Started. SSID: RoboFusion-AirGap, IP: %s\n", millis(), WiFi.softAPIP().toString().c_str());
}

/* ------------------------------------------------------------------
 * setup()
 * ------------------------------------------------------------------ */
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  gpio_reset_pin(static_cast<gpio_num_t>(IR_PIN));
  gpio_set_direction(static_cast<gpio_num_t>(IR_PIN), GPIO_MODE_INPUT);
  gpio_pullup_en(static_cast<gpio_num_t>(IR_PIN));

  dht.begin();

  Serial.println();
  Serial.println("==========================================");
  Serial.println("   RoboFusion 1.0 - Stage 3 Initialized   ");
  Serial.println("==========================================");

  /* Load NVS Credentials */
  preferences.begin("wifi_config", true);
  wifiSSID     = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  deviceName   = preferences.getString("deviceName", "RoboFusion-AirGap");
  preferences.end();

  if (wifiSSID.length() == 0) {
    Serial.printf("[%lu ms] [NVS] No saved WiFi credentials found. Entering AP Mode...\n", millis());
    startAPMode();
  } else {
    Serial.printf("[%lu ms] [NVS] Loaded SSID: '%s' | Password length: %d chars | Device: '%s'\n",
                  millis(), wifiSSID.c_str(), wifiPassword.length(), deviceName.c_str());
    Serial.printf("[%lu ms] Connecting to WiFi (15s timeout)...\n", millis());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(deviceName.c_str());
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    initWebServerRoutes();

    wifiConnectStartMs = millis();
    currentMode = MODE_STA_CONNECTING;
  }
}

/* ------------------------------------------------------------------
 * loop()  -- Non-blocking scheduler
 * ------------------------------------------------------------------ */
void loop() {
  const uint32_t now = millis();

  /* ---------------------------------------------------------------
   * Non-Blocking Scheduled Reboot Check
   * --------------------------------------------------------------- */
  if (pendingReboot && now >= rebootScheduledMs) {
    ESP.restart();
  }

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

    if (!isSensorConnected(DHT_PIN)) {
      Serial.printf("[%lu ms] [ERROR] DHT11 Sensor Disconnected!\n", now);
    } else {
      float hum = dht.readHumidity();
      float temp = dht.readTemperature();

      if (isnan(hum) || isnan(temp) || (hum == 0.0 && temp == 0.0)) {
        Serial.printf("[%lu ms] [ERROR] DHT11 Sensor Read Failed!\n", now);
      } else {
        Serial.printf("[%lu ms] DHT11 -> Temp: %.1f °C, Humidity: %.1f %%\n", now, temp, hum);
      }
    }
  }

  /* ---------------------------------------------------------------
   * Job 4 : Physical Reset Button Polling (GPIO 45)
   * --------------------------------------------------------------- */
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!buttonPressedState) {
      buttonPressedState = true;
      buttonPressStartMs = now;
    } else if (!pendingReboot && (now - buttonPressStartMs >= BUTTON_HOLD_TIME_MS)) {
      Serial.printf("[%lu ms] [RESET] Reset Button held > 2s! Clearing NVS & WiFi storage...\n", now);
      
      /* Clear Preferences NVS */
      preferences.begin("wifi_config", false);
      preferences.clear();
      preferences.end();

      /* Erase ESP-IDF internal WiFi NVS storage */
      WiFi.disconnect(true, true);

      /* Schedule immediate non-blocking reboot */
      pendingReboot = true;
      rebootScheduledMs = now + 200UL;
    }
  } else {
    buttonPressedState = false;
  }

  /* ---------------------------------------------------------------
   * Job 5 : WiFi Connection Manager & Web Server
   * --------------------------------------------------------------- */
  if (currentMode == MODE_STA_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      currentMode = MODE_STA_CONNECTED;
      Serial.printf("[%lu ms] WiFi Connected! IP Address: %s | Dashboard: http://%s/\n",
                    now, WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
    } else if (now - wifiConnectStartMs >= WIFI_TIMEOUT_MS) {
      Serial.printf("[%lu ms] WiFi Connection Failed / Timeout (15s). Switching to Setup AP Mode...\n", now);
      startAPMode();
    }
  }

  /* Handle Web Server & Captive Portal client requests */
  if (currentMode == MODE_AP_SETUP) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else if (currentMode == MODE_STA_CONNECTED) {
    server.handleClient();
  }
}