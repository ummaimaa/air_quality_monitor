# ESP32 IoT Air Quality Monitor    

A low-power IoT solution for real-time air quality monitoring. This project features an ESP32 that collects climate and CO2 data, displays it locally on an OLED, and transmits it via MQTT to a web dashboard.

##  Key Features
- **Power Optimized:** Implements Deep Sleep cycles with graceful network teardown to maximize battery life.
- **Robust Reliability:** Handled Task Watchdog Timer (WDT) triggers during radio shutdown for 99.9% uptime.
- **Full-Stack IoT:** Real-time data visualization via a custom MQTT-based web dashboard.
- **Local Display:** I2C-driven SSD1306 OLED for instant status updates.

##  Tech Stack
- **Hardware:** ESP32, DHT22 (Temp/Hum), MQ135 (Simulated via Potentiometer), SSD1306 OLED.
- **Firmware:** C++, Arduino Framework, PubSubClient, Adafruit GFX.
- **Connectivity:** MQTT (HiveMQ Broker), WiFi.
- **Frontend:** HTML5, CSS3 (IBM Plex Mono styling), Chart.js, MQTT.js.

##  System Architecture
1. **Wake up** from Deep Sleep (Timer).
2. **Initialize** I2C/Digital sensors.
3. **Connect** to WiFi and MQTT Broker.
4. **Sample** data from DHT22 and Analog MQ135 sensor.
5. **Update local OLED** and **Publish JSON payload** to MQTT.
6. **Shutdown** radio and enter **Deep Sleep** for 5 seconds.

##  Installation & Usage
1. Clone this repository.
2. Open `src/main.cpp` in VS Code (PlatformIO) or Arduino IDE.
3. Install dependencies: `Adafruit SSD1306`, `DHT sensor library`, `PubSubClient`.
4. Upload to ESP32.
5. Open `index.html` in any browser to view live data.

