#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_APDS9960.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

// ===== WiFi Configuration =====
// const char* ssid = "sandbox370";        // Replace with your WiFi name
// const char* password = "+s0a+s03!2gether?"; // Replace with your WiFi password

const char* ssid = "Snowdin";        // Replace with your WiFi name
const char* password = "homeofsans"; // Replace with your WiFi password

// ===== UDP for Monitoring =====
WiFiUDP udp;
const unsigned int localUdpPort = 4210;
char incomingPacket[255];

// PC to send monitoring data to (set to your computer's IP)
// IPAddress pcIP(10, 23, 11, 20);  // CHANGE THIS to your PC's IP address
IPAddress pcIP(192, 168, 2, 150);
const unsigned int pcUdpPort = 4211;  // Port your PC will listen on

// ===== Hardware Configuration =====
#define TCAADDR 0x70
#define LIS3DH_CS 2

Adafruit_APDS9960 apds[6];
Adafruit_LIS3DH lis = Adafruit_LIS3DH(LIS3DH_CS);
bool apdsActive[6] = {false};


void tcaSelect(uint8_t channel) {
  if (channel > 7) return;
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
  delayMicroseconds(100);
}

int getOppositeSide(int side) {
  switch(side) {
    case 0: return 5;
    case 5: return 0;
    case 1: return 3;
    case 3: return 1;
    case 2: return 4;
    case 4: return 2;
    default: return -1;
  }
}

void sendMessage(const char* message) {
  Serial.println(message);
  udp.beginPacket(pcIP, pcUdpPort);
  udp.print(message);
  udp.endPacket();
}

void sendFreakoutMessage(int brightestSide, int floorSide, int dimmestSide) {
  char freakoutMsg[128];
  snprintf(freakoutMsg, sizeof(freakoutMsg),
           "AHH, side %d is TOO BRIGHT!", brightestSide);
  sendMessage(freakoutMsg);

  delay(100);

  sendNormalMessage(floorSide, dimmestSide);
}

void sendNormalMessage(int floorSide, int dimmestSide) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer),
           "I'm standing on side %d, and I want to flip to side %d",
           floorSide, dimmestSide);
  sendMessage(buffer);
}

void setup() {
  Serial.begin(115200);

  // ===== WiFi Setup =====
  Serial.println("\n\nConnecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // ===== OTA Setup =====
  ArduinoOTA.setHostname("dimmy-cube");
  ArduinoOTA.setPassword("dimmy123");  // Optional password for security

  ArduinoOTA.onStart([]() {
    Serial.println("\nOTA Update Starting...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update Complete!");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");

  // ===== UDP Setup =====
  udp.begin(localUdpPort);
  Serial.printf("UDP listening on port %d\n", localUdpPort);

  // ===== Hardware Setup =====
  Wire.begin();

  // Initialize Accelerometer
  if (!lis.begin()) {
    Serial.println("ERROR: LIS3DH Fail");
    while (1);
  }
  lis.setRange(LIS3DH_RANGE_4_G);

  // Initialize 6 APDS9960 sensors
  for (int i = 0; i < 6; i++) {
    tcaSelect(i);
    if (apds[i].begin()) {
      apds[i].enableProximity(true);
      apds[i].enableColor(true);
      apds[i].setADCGain(APDS9960_AGAIN_64X);
      apdsActive[i] = true;
      Serial.printf("APDS sensor %d initialized\n", i);
    } else {
      Serial.printf("APDS sensor %d failed\n", i);
    }
  }

  Serial.println("\n=== DIMMY THE CUBE READY ===");
  Serial.println("Monitoring: floorSide | dimmestSide | brightestSide");
  Serial.println("============================\n");
}


void loop() {
  static unsigned long lastPrintTime = 0;
  static unsigned long lastFreakoutTime = 0;
  static unsigned long lastDataTime = 0;
  const unsigned long PRINT_INTERVAL = 3000;      // 3 seconds
  const unsigned long FREAKOUT_COOLDOWN = 1500;   // 1.5 seconds
  const unsigned long DATA_INTERVAL = 1000;       // 1 second for data monitoring
  const uint16_t BRIGHTNESS_THRESHOLD = 3000;

  ArduinoOTA.handle();  // Handle OTA updates

  sensors_event_t event;
  lis.getEvent(&event);

  uint16_t maxLight = 0;
  uint16_t minLight = 65535;
  int brightestSide = -1;
  int dimmestSide = -1;
  uint8_t maxProx = 0;
  int floorSide = -1;

  // First pass: find floor side
  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i]) continue;

    tcaSelect(i);
    uint8_t prox = apds[i].readProximity();

    if (prox > maxProx) {
      maxProx = prox;
      floorSide = i;
    }
  }

  // Find top side (opposite of floor)
  int topSide = getOppositeSide(floorSide);

  // Second pass: find brightest and dimmest (excluding floor AND top)
  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i]) continue;

    tcaSelect(i);
    uint16_t r, g, b, c;

    if (apds[i].colorDataReady()) {
      apds[i].getColorData(&r, &g, &b, &c);

      // Track brightest side (highest light)
      if (c > maxLight) {
        maxLight = c;
        brightestSide = i;
      }

      // Track dimmest side (lowest light, excluding floor and top)
      if (c < minLight && i != floorSide && i != topSide) {
        minLight = c;
        dimmestSide = i;
      }
    }
  }

  // Continuous data monitoring (every second)
  if (millis() - lastDataTime >= DATA_INTERVAL) {
    char dataMsg[128];
    snprintf(dataMsg, sizeof(dataMsg),
             "[DATA] Floor:%d | Dimmest:%d (%u) | Brightest:%d (%u)",
             floorSide, dimmestSide, minLight, brightestSide, maxLight);
    sendMessage(dataMsg);
    lastDataTime = millis();
  }

  // Check for freakout state
  bool shouldFreakout = (maxLight > BRIGHTNESS_THRESHOLD) &&
                        (millis() - lastFreakoutTime >= FREAKOUT_COOLDOWN);

  if (shouldFreakout) {
    sendFreakoutMessage(brightestSide, floorSide, dimmestSide);
    lastFreakoutTime = millis();
    lastPrintTime = millis();  // Reset print timer
  }
  // Normal 3-second interval printing
  else if (millis() - lastPrintTime >= PRINT_INTERVAL) {
    sendNormalMessage(floorSide, dimmestSide);
    lastPrintTime = millis();
  }

  delay(50); // Keep reading sensors frequently
}