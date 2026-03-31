#include <Arduino.h>

namespace {
constexpr int TEST_LINE_PIN = 35;
constexpr char TEST_SENSOR_LABEL[] = "FF";

constexpr unsigned long READ_INTERVAL_MS = 200;

unsigned long lastReadAt = 0;

void initializeLineSensor() {
  pinMode(TEST_LINE_PIN, INPUT);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  initializeLineSensor();

  Serial.println("Line sensor test ready");
  Serial.print("Testing sensor ");
  Serial.print(TEST_SENSOR_LABEL);
  Serial.print(" on GPIO ");
  Serial.println(TEST_LINE_PIN);
  Serial.println("WH-006 digital output test");
  Serial.println("Move the sensor between floor and line and watch for 0/1 changes");
}

void loop() {
  if (millis() - lastReadAt < READ_INTERVAL_MS) {
    delay(10);
    return;
  }

  lastReadAt = millis();

  Serial.print(TEST_SENSOR_LABEL);
  Serial.print(" digital=");
  Serial.println(digitalRead(TEST_LINE_PIN));
}
