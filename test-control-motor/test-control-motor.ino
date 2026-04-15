#include <Wire.h>
#include <SimpleFOC.h>

// ===== Pins =====
#define CONTROL_IN1  32  // swapped with IN3 in case soldered backwards
#define CONTROL_IN2  33
#define CONTROL_IN3  25
#define CONTROL_EN   27
#define ENCODER_SDA  21
#define ENCODER_SCL  22

// ===== Motor constants =====
const int   POLE_PAIRS          = 7;
const float VOLTAGE_SUPPLY      = 12;
const float VOLTAGE_LIMIT       = 4;    // matched to working example
const float VOLTAGE_SENSOR_ALIGN = 4;   // voltage used during initFOC alignment
const float VELOCITY_LIMIT      = 40;  // rad/s — was 5, too restrictive
const float P_ANGLE             = 10;   // outer position loop — lower = less aggressive at rest
const float PID_P               = 0.2;  // inner velocity loop P
const float PID_I               = 5;    // inner velocity loop I
const float LPF_TF              = 0.05; // velocity smoothing — higher = less jitter at rest

BLDCMotor      motor  = BLDCMotor(POLE_PAIRS);
BLDCDriver3PWM driver = BLDCDriver3PWM(CONTROL_IN1, CONTROL_IN2, CONTROL_IN3, CONTROL_EN);
MagneticSensorI2C encoder = MagneticSensorI2C(AS5600_I2C);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Control Motor Test ===");

  Wire1.begin(ENCODER_SDA, ENCODER_SCL);
  Wire1.setClock(400000);
  SimpleFOCDebug::enable(&Serial);

  // ===== Step 1: Check AS5600 is reachable on Wire1 =====
  Wire1.beginTransmission(0x36);
  byte error = Wire1.endTransmission();
  if (error == 0) {
    Serial.println("[AS5600] Found at 0x36 on Wire1 (GPIO21/22) ✓");
  } else {
    Serial.printf("[AS5600] NOT found on Wire1 (error %d) — check wiring!\n", error);
  }

  // ===== Step 2: Init encoder =====
  encoder.init(&Wire1);
  Serial.printf("[ENCODER] Initial angle: %.2f rad (%.1f deg)\n",
                encoder.getAngle(), encoder.getAngle() * 180.0 / PI);

  // ===== Step 4: Init driver and motor =====
  driver.voltage_power_supply = VOLTAGE_SUPPLY;
  if (driver.init()) {
    Serial.println("[DRIVER] Initialized ✓");
  } else {
    Serial.println("[DRIVER] Init FAILED — check IN1/IN2/IN3/EN pins!");
  }

  motor.linkDriver(&driver);
  motor.linkSensor(&encoder);
  motor.voltage_sensor_align = VOLTAGE_SENSOR_ALIGN;
  motor.voltage_limit        = VOLTAGE_LIMIT;
  motor.velocity_limit       = VELOCITY_LIMIT;
  motor.controller           = MotionControlType::angle;
  motor.P_angle.P            = P_ANGLE;
  motor.PID_velocity.P       = PID_P;
  motor.PID_velocity.I       = PID_I;
  motor.PID_velocity.D       = 0;
  motor.PID_velocity.output_ramp = 0;
  motor.LPF_velocity.Tf      = LPF_TF;
  motor.init();
  Serial.println("[MOTOR] Initialized");

  // ===== Step 4: initFOC (encoder alignment) =====
  Serial.println("[MOTOR] Running initFOC — motor should twitch/move briefly...");
  motor.initFOC();
  Serial.println("[MOTOR] initFOC complete ✓");

  Serial.println("\nStarting movement test: alternating 0° <-> 90° every 2 seconds");
  Serial.println("Watch the motor and Serial output.\n");
}

float targetAngle = 0;
unsigned long lastSwitch = 0;

void loop() {
  // Switch target every 2 seconds
  if (millis() - lastSwitch >= 2000) {
    targetAngle = (targetAngle == 0) ? PI / 2 : 0;
    Serial.printf("[TARGET] Moving to %.0f deg\n", targetAngle * 180.0 / PI);
    lastSwitch = millis();
  }

  motor.loopFOC();
  motor.move(targetAngle);

  // Print encoder angle every 500ms
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 500) {
    float angle = encoder.getAngle();
    float error = abs(angle - targetAngle);
    Serial.printf("[ENCODER] Angle: %.2f rad (%.1f deg) | Target: %.0f deg | Error: %.2f rad\n",
                  angle, angle * 180.0 / PI,
                  targetAngle * 180.0 / PI, error);
    lastPrint = millis();
  }
}
