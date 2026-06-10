#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "esp_wifi.h"
#include "esp_task_wdt.h"
#include "esp_bt.h"         // FIX: needed for esp_bt_controller_disable()

// --- Pin Definitions ---
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ135_PIN 34

// --- OLED Settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Sensor Objects ---
DHT dht(DHTPIN, DHTTYPE);

// --- WiFi & MQTT Settings ---
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_SERVER = "broker.hivemq.com";
const char* MQTT_TOPIC = "portfolio/airquality/data";
const char* MQTT_ALERT_TOPIC = "portfolio/airquality/alerts";

WiFiClient espClient;
PubSubClient client(espClient);

// --- Deep Sleep Settings ---
#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP  5

// ---------------------------------------------------------------
// WiFi Setup
// ---------------------------------------------------------------
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    esp_task_wdt_reset();
    delay(500);
    Serial.print(".");
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

// ---------------------------------------------------------------
// MQTT Reconnect — chunked retry to keep WDT fed
// ---------------------------------------------------------------
void reconnect() {
  int retries = 0;
  while (!client.connected() && retries < 3) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      retries++;

      unsigned long waitStart = millis();
      while (millis() - waitStart < 2000) {
        esp_task_wdt_reset();
        client.loop();
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }
}

// ---------------------------------------------------------------
// Deep Sleep Shutdown
// FIX: esp_task_wdt_delete(NULL) is unreliable in Wokwi —
// the simulator still fires TG1WDT_SYS_RESET during radio teardown.
// Solution: fully deinit the WDT (esp_task_wdt_deinit) BEFORE
// touching the radio, so no watchdog exists to fire.
// Also disable BT controller to silence remaining system tasks.
// ---------------------------------------------------------------
void goToDeepSleep() {
  Serial.println("Preparing network for shutdown...");

  // Step 1: Fully deinit the Task Watchdog FIRST — before any radio ops
  // This is stronger than esp_task_wdt_delete(NULL) which only removes
  // the current task but leaves the WDT hardware running
  esp_task_wdt_deinit();

  // Step 2: Disable Bluetooth controller to stop BT background tasks
  // that can keep the system active and re-trigger the WDT
  esp_bt_controller_disable();

  // Step 3: Gracefully disconnect MQTT
  if (client.connected()) {
    client.disconnect();
  }

  // Step 4: Disconnect WiFi and kill radio
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();

  // Step 5: Yield to FreeRTOS — lets all background tasks see the
  // radio is gone and exit cleanly before CPU halts
  vTaskDelay(pdMS_TO_TICKS(200));

  // Step 6: Flush serial and enter deep sleep
  Serial.println("Entering Deep Sleep cleanly...");
  Serial.flush();

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
  // Execution stops here — wakes after TIME_TO_SLEEP seconds
}

// ---------------------------------------------------------------
// Setup
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("\n--- Woke up from Deep Sleep! ---");
  } else {
    Serial.println("\n--- Starting Air Quality Monitor (Cold Boot) ---");
  }

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed!"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("Booting up...");
  display.display();

  // Initialize Sensors
  dht.begin();
  pinMode(MQ135_PIN, INPUT);

  // Initialize Network
  setup_wifi();
  client.setServer(MQTT_SERVER, 1883);

  if (WiFi.status() == WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("WiFi Connected!");
    display.display();
    delay(500);
  }
}

// ---------------------------------------------------------------
// Loop — runs once per wake cycle then sleeps
// ---------------------------------------------------------------
void loop() {
  // 1. Maintain MQTT Connection
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }

  // 2. Read Sensors
  float humidity = dht.readHumidity();
  float temp = dht.readTemperature();
  int airQualityRaw = analogRead(MQ135_PIN);
  int estimatedCO2 = map(airQualityRaw, 0, 4095, 400, 2000);

  if (isnan(humidity) || isnan(temp)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    Serial.print("Temp: "); Serial.print(temp); Serial.print("C | ");
    Serial.print("Hum: ");  Serial.print(humidity); Serial.print("% | ");
    Serial.print("CO2 (est): "); Serial.print(estimatedCO2); Serial.println(" ppm");

    // Alert Threshold Logic
    bool alertTriggered = false;
    if (estimatedCO2 >= 1000) {
      alertTriggered = true;
      if (client.connected()) {
        client.publish(MQTT_ALERT_TOPIC, "WARNING: High CO2 Threshold Breached!");
        Serial.println("--> ALERT: High CO2 warning published!");
      }
    }

    // Update OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Air Quality Monitor");
    display.drawLine(0, 10, 128, 10, WHITE);
    display.setCursor(0, 18);
    display.print("Temp: "); display.print(temp); display.println(" C");
    display.setCursor(0, 32);
    display.print("Hum:  "); display.print(humidity); display.println(" %");
    display.setCursor(0, 46);
    display.print("CO2:  "); display.print(estimatedCO2); display.println(" ppm");

    if (alertTriggered) {
      display.setCursor(0, 56);
      display.print("! HIGH CO2 ALERT !");
    }
    display.display();

    // Publish to MQTT
    if (client.connected()) {
      String payload = "{";
      payload += "\"temperature\":" + String(temp) + ",";
      payload += "\"humidity\":"    + String(humidity) + ",";
      payload += "\"co2\":"         + String(estimatedCO2);
      payload += "}";

      if (client.publish(MQTT_TOPIC, payload.c_str())) {
        Serial.println("Published to MQTT broker.");
      }
    }
  }

  // 3. Shutdown and sleep
  goToDeepSleep();
}
