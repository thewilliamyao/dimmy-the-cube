#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_APDS9960.h>

// ===== WiFi Configuration =====
const char* ssid = "sandbox370";
const char* password = "+s0a+s03!2gether?";

// const char* ssid = "Snowdin";
// const char* password = "homeofsans";

// ===== UDP Configuration =====
WiFiUDP udp;
const unsigned int localUdpPort = 4210;
char incomingPacket[255];

IPAddress pcIP(10, 23, 11, 20);
// IPAddress pcIP(192, 168, 2, 150);
const unsigned int pcUdpPort = 4211;

// ===== Tunable Constants =====
const unsigned long PRINT_INTERVAL    = 3000;  // ms between normal state messages
const unsigned long FREAKOUT_COOLDOWN = 1500;  // ms between freakout triggers
const unsigned long DATA_INTERVAL     = 1000;  // ms between debug data output
const uint16_t BRIGHTNESS_THRESHOLD  = 3000;  // maxLight above this triggers freakout
const uint16_t DARK_ENOUGH_THRESHOLD = 18;    // avgLight below this = resting state
const uint8_t  PROX_TOO_CLOSE        = 12;   // proximity above this blocks a side (0-255)

// ===== Hardware Configuration =====
#define TCAADDR 0x70

Adafruit_APDS9960 apds[6];
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

void sendNormalMessage(int floorSide, int dimmestSide) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer),
           "I'm standing on side %d, and I want to flip to side %d",
           floorSide, dimmestSide);
  sendMessage(buffer);
}

void sendFreakoutMessage(int brightestSide, int floorSide, int dimmestSide) {
  char freakoutMsg[128];
  snprintf(freakoutMsg, sizeof(freakoutMsg),
           "AHH, side %d is TOO BRIGHT!", brightestSide);
  sendMessage(freakoutMsg);

  delay(100);

  sendNormalMessage(floorSide, dimmestSide);
}

void sendRestingMessage() {
  sendMessage("I like this spot, I'm now glowing");
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
  ArduinoOTA.setPassword("dimmy123");

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

  // ===== Sensor Setup =====
  Wire.begin();

  for (int i = 0; i < 6; i++) {
    tcaSelect(i);
    if (apds[i].begin()) {
      apds[i].enableProximity(true);
      apds[i].enableColor(true);
      apds[i].setADCGain(APDS9960_AGAIN_64X);
      apds[i].setProxGain(APDS9960_PGAIN_8X);
      apdsActive[i] = true;
      Serial.printf("APDS sensor %d initialized\n", i);
    } else {
      Serial.printf("APDS sensor %d failed\n", i);
    }
  }

  Serial.println("\n=== DIMMY THE CUBE READY ===\n");
}


void loop() {
  static unsigned long lastPrintTime = 0;
  static unsigned long lastFreakoutTime = 0;
  static unsigned long lastDataTime = 0;

  ArduinoOTA.handle();

  uint16_t lightLevels[6] = {65535, 65535, 65535, 65535, 65535, 65535};
  uint8_t proxLevels[6] = {0};
  uint16_t maxLight = 0;
  int brightestSide = -1;
  int dimmestSide = -1;
  uint8_t maxProx = 0;
  int floorSide = -1;

  // First pass: find floor side
  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i]) continue;

    tcaSelect(i);
    proxLevels[i] = apds[i].readProximity();

    if (proxLevels[i] > maxProx) {
      maxProx = proxLevels[i];
      floorSide = i;
    }
  }

  // Find top side (opposite of floor)
  int topSide = getOppositeSide(floorSide);

  // Second pass: read light levels on all sides (excluding floor)
  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i] || i == floorSide) continue;

    tcaSelect(i);
    uint16_t r, g, b, c;

    if (apds[i].colorDataReady()) {
      apds[i].getColorData(&r, &g, &b, &c);
      lightLevels[i] = c;

      if (c > maxLight) {
        maxLight = c;
        brightestSide = i;
      }
    }
  }

  // Find dimmest side: skip floor, top, and sides with proximity too close
  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i] || i == floorSide || i == topSide) continue;
    if (proxLevels[i] >= PROX_TOO_CLOSE) continue;

    if (dimmestSide == -1 || lightLevels[i] < lightLevels[dimmestSide]) {
      dimmestSide = i;
    }
  }

  // Calculate average brightness across active non-floor sides (including top)
  uint32_t lightSum = 0;
  int lightCount = 0;
  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i] || i == floorSide) continue;
    if (lightLevels[i] == 65535) continue;  // Skip unread sides
    lightSum += lightLevels[i];
    lightCount++;
  }
  uint16_t avgLight = lightCount > 0 ? lightSum / lightCount : 0;

  // Debug monitoring (every second)
  if (millis() - lastDataTime >= DATA_INTERVAL) {
    char dataMsg[128];
    snprintf(dataMsg, sizeof(dataMsg),
             "[DATA] Floor:%d | Dimmest:%d (%u) | Brightest:%d (%u) | Avg:%u %s",
             floorSide, dimmestSide, dimmestSide != -1 ? lightLevels[dimmestSide] : 0, brightestSide, maxLight,
             avgLight, avgLight < DARK_ENOUGH_THRESHOLD ? "(DARK ENOUGH)" : "");
    sendMessage(dataMsg);

    char lightMsg[128];
    int lightOffset = snprintf(lightMsg, sizeof(lightMsg), "[LIGHT]");
    for (int i = 0; i < 6; i++) {
      if (i == floorSide) { lightOffset += snprintf(lightMsg + lightOffset, sizeof(lightMsg) - lightOffset, " S%d:FLOOR", i); continue; }
      if (i == topSide)   { lightOffset += snprintf(lightMsg + lightOffset, sizeof(lightMsg) - lightOffset, " S%d:TOP:%u", i, lightLevels[i]); continue; }
      lightOffset += snprintf(lightMsg + lightOffset, sizeof(lightMsg) - lightOffset,
                              " S%d:%u%s", i, lightLevels[i],
                              i == dimmestSide ? "(DIMMEST)" : "");
    }
    sendMessage(lightMsg);

    char proxMsg[128];
    int proxOffset = snprintf(proxMsg, sizeof(proxMsg), "[PROX]");
    for (int i = 0; i < 6; i++) {
      if (i == floorSide || i == topSide) continue;
      proxOffset += snprintf(proxMsg + proxOffset, sizeof(proxMsg) - proxOffset,
                         " S%d:%u%s", i, proxLevels[i],
                         proxLevels[i] >= PROX_TOO_CLOSE ? "(BLOCKED)" : "");
    }
    sendMessage(proxMsg);

    lastDataTime = millis();
  }

  bool darkEnough = (avgLight < DARK_ENOUGH_THRESHOLD);

  // Freakout takes priority over everything, including dark enough state
  bool shouldFreakout = (maxLight > BRIGHTNESS_THRESHOLD) &&
                        (millis() - lastFreakoutTime >= FREAKOUT_COOLDOWN);

  if (shouldFreakout) {
    sendFreakoutMessage(brightestSide, floorSide, dimmestSide);
    lastFreakoutTime = millis();
    lastPrintTime = millis();
  }
  else if (millis() - lastPrintTime >= PRINT_INTERVAL) {
    if (darkEnough) {
      sendRestingMessage();
    } else {
      sendNormalMessage(floorSide, dimmestSide);
    }
    lastPrintTime = millis();
  }

  delay(50);
}
