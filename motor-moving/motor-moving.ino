#include <ESP32Servo.h>

const int potPin = 34;
const int escPin = 25;
const int buttonPin = 35;

Servo esc;

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT);  // button connects pin to GND when pressed

  esc.attach(escPin, 1000, 2000);
  esc.writeMicroseconds(1000);
  delay(3000);
  Serial.println("Armed!");
}

void loop() {
  bool braking = digitalRead(buttonPin) == LOW;  // LOW = pressed

  if (braking) {
    esc.writeMicroseconds(1000);  // instant stop command
    Serial.println("BRAKING");
  } else {
    int potValue = analogRead(potPin);
    int escValue = map(potValue, 0, 4095, 1000, 2000);
    esc.writeMicroseconds(escValue);
    Serial.print("ESC µs: ");
    Serial.println(escValue);
  }

  delay(20);
}