#include <Wire.h>
#include <Adafruit_APDS9960.h>
#include <SimpleFOC.h>
#include <ESP32Servo.h>

// ===== Toggle WiFi (uncomment to enable WiFi + OTA + UDP monitoring) =====
#define WIFI_ENABLED

#ifdef WIFI_ENABLED
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>

const char* ssid     = "sandbox370";
const char* password = "+s0a+s03!2gether?";

// const char* ssid     = "Snowdin";
// const char* password = "homeofsans";

WiFiUDP udp;
IPAddress pcIP(10, 20, 76, 227);
// IPAddress pcIP(192, 168, 2, 150);
const unsigned int localUdpPort = 4210;
const unsigned int pcUdpPort    = 4211;
#endif

// ===== Tunable Constants =====
const unsigned long PRINT_INTERVAL       = 5000;  // ms between normal state messages
const unsigned long FREAKOUT_COOLDOWN    = 1500;  // ms between freakout triggers
const unsigned long DATA_INTERVAL        = 1000;  // ms between debug data output
const unsigned long SENSOR_INTERVAL      = 100;   // ms between sensor reads (keeps FOC fast)
const uint16_t     BRIGHTNESS_THRESHOLD  = 3000;  // maxLight above this triggers freakout
const uint16_t     DARK_ENOUGH_THRESHOLD = 18;    // avgLight below this = resting state
const uint8_t      PROX_TOO_CLOSE        = 12;    // proximity above this blocks a side (0-255)
const unsigned long SPIN_DURATION        = 1000;  // ms main motor spins
const int ESC_FORWARD = 1925;  // 85% of full forward (2000)
const int ESC_REVERSE = 1075;  // 85% of full reverse (1000)
const unsigned long POSITION_DURATION    = 500;   // ms to wait for control motor to reach angle
const unsigned long BRAKE_DURATION       = 1000;  // ms at neutral before returning to sensing

// ===== Motor Constants =====
const int   CONTROL_POLE_PAIRS          = 7;    // 2804 gimbal motor: 12N14P = 14 poles / 2 = 7 pole pairs
const float CONTROL_VOLTAGE_SUPPLY      = 12;   // SimpleFOCmini supply voltage
const float CONTROL_VOLTAGE_LIMIT       = 4;
const float CONTROL_VOLTAGE_SENSOR_ALIGN = 4;   // voltage used during initFOC alignment
const float CONTROL_VELOCITY_LIMIT      = 40;   // rad/s
const float CONTROL_P_ANGLE             = 10;   // outer position loop P gain
const float CONTROL_PID_P               = 0.2;  // inner velocity loop P gain
const float CONTROL_PID_I               = 5;    // inner velocity loop I gain
const float CONTROL_PID_D               = 0;
const float CONTROL_LPF_TF              = 0.05; // velocity low-pass filter

// ===== Pin Definitions =====
#define CONTROL_IN1  25
#define CONTROL_IN2  33
#define CONTROL_IN3  32
#define CONTROL_EN   27
#define ESC_PIN      26
// Wire1 (dedicated I2C bus for AS5600 — avoids contention with APDS sensors on Wire)
#define ENCODER_SDA  16
#define ENCODER_SCL  17

// ===== Hardware Config =====
#define TCAADDR 0x70

Adafruit_APDS9960 apds[6];
bool apdsActive[6] = {false};

BLDCMotor      controlMotor  = BLDCMotor(CONTROL_POLE_PAIRS);
BLDCDriver3PWM controlDriver = BLDCDriver3PWM(CONTROL_IN1, CONTROL_IN2, CONTROL_IN3, CONTROL_EN);
MagneticSensorI2C encoder    = MagneticSensorI2C(AS5600_I2C);
Servo esc;

// ===== State Machine =====
enum CubeState { SENSING, POSITIONING, SPINNING, BRAKING };
CubeState cubeState = SENSING;
unsigned long stateStartTime = 0;
float controlTargetAngle = 0;  // Always-live FOC target, updated by state machine

struct MoveCommand {
  float controlAngle;    // radians: 0 or PI/2
  int   escMicroseconds; // 2000 = full forward, 1000 = full reverse
};
MoveCommand pendingMove = {0, 1500};

// ===== Helpers =====

void tcaSelect(uint8_t channel) {
  if (channel > 7) return;
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
  delayMicroseconds(100);
}

int getOppositeSide(int side) {
  switch (side) {
    case 0: return 5;  case 5: return 0;
    case 1: return 3;  case 3: return 1;
    case 2: return 4;  case 4: return 2;
    default: return -1;
  }
}

void sendMessage(const char* message) {
  Serial.println(message);
#ifdef WIFI_ENABLED
  udp.beginPacket(pcIP, pcUdpPort);
  udp.print(message);
  udp.endPacket();
#endif
}

void sendNormalMessage(int floorSide, int dimmestSide) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer),
           "I'm standing on side %d, and I want to flip to side %d",
           floorSide, dimmestSide);
  sendMessage(buffer);
}

void sendFreakoutMessage(int brightestSide, int floorSide, int dimmestSide) {
  char msg[128];
  snprintf(msg, sizeof(msg), "AHH, side %d is TOO BRIGHT!", brightestSide);
  sendMessage(msg);
  delay(100);
  sendNormalMessage(floorSide, dimmestSide);
}

void sendRestingMessage() {
  sendMessage("I like this spot, I'm now glowing");
}

// ===== Movement Logic =====

bool isDirectMove(int floorSide, int targetSide) {
  if (floorSide == 0 || floorSide == 5) return true;
  return (targetSide == 0 || targetSide == 5);
}

MoveCommand getDirectMoveCommand(int floorSide, int targetSide) {
  switch (floorSide) {
    case 0:
      if (targetSide == 2) return {0,    ESC_FORWARD};
      if (targetSide == 4) return {0,    ESC_REVERSE};
      if (targetSide == 3) return {PI/2, ESC_FORWARD};
      if (targetSide == 1) return {PI/2, ESC_REVERSE};
      break;
    case 5:  // Control motor is upside down — main motor direction is reversed
      if (targetSide == 2) return {0,    ESC_REVERSE};
      if (targetSide == 4) return {0,    ESC_FORWARD};
      if (targetSide == 3) return {PI/2, ESC_REVERSE};
      if (targetSide == 1) return {PI/2, ESC_FORWARD};
      break;
    case 4:
      if (targetSide == 0) return {0, 2000};
      if (targetSide == 5) return {0, 1000};
      break;
    case 2:
      if (targetSide == 5) return {0, 2000};
      if (targetSide == 0) return {0, 1000};
      break;
    case 1:
      if (targetSide == 5) return {0, 2000};
      if (targetSide == 0) return {0, 1000};
      break;
    case 3:
      if (targetSide == 0) return {0, 2000};
      if (targetSide == 5) return {0, 1000};
      break;
  }
  return {0, 1500};  // fallback: no-op
}

int getIntermediateSide(uint16_t lightLevels[]) {
  return (lightLevels[0] <= lightLevels[5]) ? 0 : 5;
}

void triggerMove(int floorSide, int targetSide, uint16_t lightLevels[]) {
  int resolvedTarget = targetSide;

  if (!isDirectMove(floorSide, targetSide)) {
    resolvedTarget = getIntermediateSide(lightLevels);
    char msg[128];
    snprintf(msg, sizeof(msg),
             "[MOVE] Side %d unreachable from floor %d, routing via side %d",
             targetSide, floorSide, resolvedTarget);
    sendMessage(msg);
  }

  pendingMove = getDirectMoveCommand(floorSide, resolvedTarget);
  controlTargetAngle = pendingMove.controlAngle;

  char msg[128];
  snprintf(msg, sizeof(msg),
           "[MOVE] Floor:%d -> Target:%d | ControlAngle:%.0f deg | ESC:%dus",
           floorSide, resolvedTarget,
           pendingMove.controlAngle * 180.0 / PI,
           pendingMove.escMicroseconds);
  sendMessage(msg);

  cubeState      = POSITIONING;
  stateStartTime = millis();
}

// ===== State Machine Tick =====
// FOC runs unconditionally in loop() — state machine only manages transitions

void tickStateMachine() {
  unsigned long elapsed = millis() - stateStartTime;

  switch (cubeState) {
    case POSITIONING:
      if (elapsed >= POSITION_DURATION) {
        esc.writeMicroseconds(pendingMove.escMicroseconds);
        sendMessage("[STATE] Spinning main motor");
        cubeState      = SPINNING;
        stateStartTime = millis();
      }
      break;

    case SPINNING:
      if (elapsed >= SPIN_DURATION) {
        esc.writeMicroseconds(1500);
        sendMessage("[STATE] Braking");
        cubeState      = BRAKING;
        stateStartTime = millis();
      }
      break;

    case BRAKING:
      if (elapsed >= BRAKE_DURATION) {
        sendMessage("[STATE] Back to sensing");
        cubeState = SENSING;
      }
      break;

    case SENSING:
      break;
  }
}

// ===== Setup =====

void setup() {
  Serial.begin(115200);

  // ESC arming — must happen before WiFi, ESC expects neutral at power-up
  esc.attach(ESC_PIN, 1000, 2000);
  esc.writeMicroseconds(1500);
  Serial.println("Arming ESC...");
  delay(3000);
  Serial.println("ESC Armed!");

  // Wire: TCA + APDS sensors (channels 0-5)
  Wire.begin();
  Wire.setClock(400000);

  // Wire1: AS5600 encoder only — dedicated bus, no TCA, no contention
  Wire1.begin(ENCODER_SDA, ENCODER_SCL);
  Wire1.setClock(400000);

  // Control motor (SimpleFOC, AS5600 on Wire1)
  encoder.init(&Wire1);

  controlDriver.voltage_power_supply  = CONTROL_VOLTAGE_SUPPLY;
  controlDriver.init();
  controlMotor.linkDriver(&controlDriver);
  controlMotor.linkSensor(&encoder);
  controlMotor.voltage_sensor_align   = CONTROL_VOLTAGE_SENSOR_ALIGN;
  controlMotor.voltage_limit          = CONTROL_VOLTAGE_LIMIT;
  controlMotor.velocity_limit         = CONTROL_VELOCITY_LIMIT;
  controlMotor.controller             = MotionControlType::angle;
  controlMotor.P_angle.P              = CONTROL_P_ANGLE;
  controlMotor.PID_velocity.P         = CONTROL_PID_P;
  controlMotor.PID_velocity.I         = CONTROL_PID_I;
  controlMotor.PID_velocity.D         = CONTROL_PID_D;
  controlMotor.PID_velocity.output_ramp = 0;
  controlMotor.LPF_velocity.Tf        = CONTROL_LPF_TF;
  controlMotor.init();

  controlMotor.initFOC();
  Serial.println("Control motor ready");

#ifdef WIFI_ENABLED
  Serial.println("\nConnecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname("dimmy-cube");
  ArduinoOTA.setPassword("dimmy123");
  ArduinoOTA.onStart([]()   { Serial.println("\nOTA Update Starting..."); });
  ArduinoOTA.onEnd([]()     { Serial.println("\nOTA Update Complete!"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]\n", error);
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");

  udp.begin(localUdpPort);
  Serial.printf("UDP listening on port %d\n", localUdpPort);
#endif

  for (int i = 0; i < 6; i++) {
    tcaSelect(i + 1);  // channels 1-6 (channel 0 broken)
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

// ===== Loop =====

// Persistent sensor state — updated every SENSOR_INTERVAL, used for decisions
static uint16_t lightLevels[6]  = {65535, 65535, 65535, 65535, 65535, 65535};
static uint8_t  proxLevels[6]   = {0};
static uint16_t maxLight        = 0;
static int      brightestSide   = -1;
static int      dimmestSide     = -1;
static int      floorSide       = -1;
static int      topSide         = -1;
static uint16_t avgLight        = 0;
static bool     darkEnough      = false;

void readSensors() {
  for (int i = 0; i < 6; i++) lightLevels[i] = 65535;
  maxLight = 0; brightestSide = -1; dimmestSide = -1;
  uint8_t maxProx = 0; floorSide = -1;

  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i]) continue;
    tcaSelect(i + 1);  // channels 1-6
    proxLevels[i] = apds[i].readProximity();
    if (proxLevels[i] > maxProx) { maxProx = proxLevels[i]; floorSide = i; }
  }

  topSide = getOppositeSide(floorSide);

  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i] || i == floorSide) continue;
    tcaSelect(i + 1);  // channels 1-6
    uint16_t r, g, b, c;
    if (apds[i].colorDataReady()) {
      apds[i].getColorData(&r, &g, &b, &c);
      lightLevels[i] = c;
      if (c > maxLight) { maxLight = c; brightestSide = i; }
    }
  }

  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i] || i == floorSide || i == topSide) continue;
    // if (proxLevels[i] >= PROX_TOO_CLOSE) continue;
    if (dimmestSide == -1 || lightLevels[i] < lightLevels[dimmestSide]) dimmestSide = i;
  }

  uint32_t lightSum = 0; int lightCount = 0;
  for (int i = 0; i < 6; i++) {
    if (!apdsActive[i] || i == floorSide || lightLevels[i] == 65535) continue;
    lightSum += lightLevels[i]; lightCount++;
  }
  avgLight  = lightCount > 0 ? lightSum / lightCount : 0;
  darkEnough = (avgLight < DARK_ENOUGH_THRESHOLD);
}

void loop() {
  // FOC runs every iteration — same as test-control-motor
  controlMotor.loopFOC();
  controlMotor.move(controlTargetAngle);

#ifdef WIFI_ENABLED
  ArduinoOTA.handle();
#endif
  tickStateMachine();

  // Sensor reads gated to keep FOC loop fast
  static unsigned long lastSensorTime = 0;
  if (millis() - lastSensorTime < SENSOR_INTERVAL) return;
  lastSensorTime = millis();
  readSensors();

  static unsigned long lastPrintTime    = 0;
  static unsigned long lastFreakoutTime = 0;
  static unsigned long lastDataTime     = 0;

  if (millis() - lastDataTime >= DATA_INTERVAL) {
    char dataMsg[128];
    snprintf(dataMsg, sizeof(dataMsg),
             "[DATA] Floor:%d | Dimmest:%d (%u) | Brightest:%d (%u) | Avg:%u %s",
             floorSide, dimmestSide,
             dimmestSide != -1 ? lightLevels[dimmestSide] : 0,
             brightestSide, maxLight,
             avgLight, darkEnough ? "(DARK ENOUGH)" : "");
    sendMessage(dataMsg);

    char lightMsg[128];
    int lo = snprintf(lightMsg, sizeof(lightMsg), "[LIGHT]");
    for (int i = 0; i < 6; i++) {
      if      (i == floorSide) lo += snprintf(lightMsg + lo, sizeof(lightMsg) - lo, " S%d:FLOOR", i);
      else if (i == topSide)   lo += snprintf(lightMsg + lo, sizeof(lightMsg) - lo, " S%d:TOP:%u", i, lightLevels[i]);
      else                     lo += snprintf(lightMsg + lo, sizeof(lightMsg) - lo, " S%d:%u%s", i, lightLevels[i], i == dimmestSide ? "(DIMMEST)" : "");
    }
    sendMessage(lightMsg);

    char proxMsg[128];
    int po = snprintf(proxMsg, sizeof(proxMsg), "[PROX]");
    for (int i = 0; i < 6; i++) {
      if (i == floorSide || i == topSide) continue;
      po += snprintf(proxMsg + po, sizeof(proxMsg) - po,
                     " S%d:%u%s", i, proxLevels[i],
                     proxLevels[i] >= PROX_TOO_CLOSE ? "(BLOCKED)" : "");
    }
    sendMessage(proxMsg);

    lastDataTime = millis();
  }

  if (cubeState != SENSING) return;

  bool shouldFreakout = (maxLight > BRIGHTNESS_THRESHOLD) &&
                        (millis() - lastFreakoutTime >= FREAKOUT_COOLDOWN);

  if (shouldFreakout) {
    sendFreakoutMessage(brightestSide, floorSide, dimmestSide);
    if (dimmestSide != -1) triggerMove(floorSide, dimmestSide, lightLevels);
    lastFreakoutTime = millis();
    lastPrintTime    = millis();
  }
  else if (millis() - lastPrintTime >= PRINT_INTERVAL) {
    if (darkEnough) {
      sendRestingMessage();
    } else if (dimmestSide != -1) {
      sendNormalMessage(floorSide, dimmestSide);
      triggerMove(floorSide, dimmestSide, lightLevels);
    }
    lastPrintTime = millis();
  }
}
