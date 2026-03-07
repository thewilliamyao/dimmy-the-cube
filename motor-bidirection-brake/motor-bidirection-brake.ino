#include <ESP32Servo.h>

const int potPin = 34;
const int escPin = 25;
const int buttonPin = 35;
const int reverseBtnPin = 32;  // new button for toggling direction

Servo esc;
bool reverseMode = false;
bool lastBtnState = HIGH;

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT);
  pinMode(reverseBtnPin, INPUT_PULLUP);

  esc.attach(escPin, 1000, 2000);
  esc.writeMicroseconds(1500);  // neutral, not 1000
  delay(3000);
  Serial.println("Armed!");
}

void loop() {
  // Toggle reverse on button press (with simple debounce)
  bool currentBtnState = digitalRead(reverseBtnPin);
  if (currentBtnState == LOW && lastBtnState == HIGH) {
    reverseMode = !reverseMode;
    Serial.print("Direction: ");
    Serial.println(reverseMode ? "REVERSE" : "FORWARD");
    delay(50);  // debounce
  }
  lastBtnState = currentBtnState;

  bool braking = digitalRead(buttonPin) == LOW;

  if (braking) {
    esc.writeMicroseconds(1500);  // neutral = stop
    Serial.println("BRAKING");
  } else {
    int potValue = analogRead(potPin);
    int escValue;

    if (reverseMode) {
      // pot 0→4095 maps to 1500→1000 (neutral to full reverse)
      escValue = map(potValue, 0, 4095, 1500, 1000);
    } else {
      // pot 0→4095 maps to 1500→2000 (neutral to full forward)
      escValue = map(potValue, 0, 4095, 1500, 2000);
    }

    esc.writeMicroseconds(escValue);
    Serial.print(reverseMode ? "REV " : "FWD ");
    Serial.print("ESC µs: ");
    Serial.println(escValue);
  }

  delay(20);
}