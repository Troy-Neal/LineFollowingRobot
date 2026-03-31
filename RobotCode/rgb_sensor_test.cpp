#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr int RGB_SDA_PIN = 21;
constexpr int RGB_SCL_PIN = 22;
constexpr uint8_t TCS34725_ADDRESS = 0x29;
constexpr uint8_t TCS34725_COMMAND_BIT = 0x80;
constexpr uint8_t TCS34725_ENABLE = 0x00;
constexpr uint8_t TCS34725_ATIME = 0x01;
constexpr uint8_t TCS34725_CONTROL = 0x0F;
constexpr uint8_t TCS34725_ID = 0x12;
constexpr uint8_t TCS34725_CDATAL = 0x14;
constexpr uint8_t TCS34725_RDATAL = 0x16;
constexpr uint8_t TCS34725_GDATAL = 0x18;
constexpr uint8_t TCS34725_BDATAL = 0x1A;
constexpr uint8_t TCS34725_ENABLE_PON = 0x01;
constexpr uint8_t TCS34725_ENABLE_AEN = 0x02;
constexpr uint8_t TCS34725_INTEGRATION_154MS = 0xC0;
constexpr uint8_t TCS34725_GAIN_16X = 0x02;
constexpr unsigned long READ_INTERVAL_MS = 500;

enum COLOUR { BLACK, WHITE, BLUE, RED, GREEN, YELLOW };

struct RgbReading {
  uint16_t red;
  uint16_t green;
  uint16_t blue;
  uint16_t clear;
};

unsigned long lastReadAt = 0;
bool rgbSensorReady = false;

bool isI2CDeviceAvailable(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool writeRgbRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(TCS34725_COMMAND_BIT | reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRgbRegister8(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(TCS34725_COMMAND_BIT | reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(static_cast<int>(TCS34725_ADDRESS), 1) != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool readRgbRegister16(uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(TCS34725_COMMAND_BIT | reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(static_cast<int>(TCS34725_ADDRESS), 2) != 2) {
    return false;
  }

  const uint8_t low = Wire.read();
  const uint8_t high = Wire.read();
  value = static_cast<uint16_t>(high << 8) | low;
  return true;
}

bool initializeRgbSensor() {
  uint8_t sensorId = 0;

  if (!isI2CDeviceAvailable(TCS34725_ADDRESS)) {
    return false;
  }

  if (!readRgbRegister8(TCS34725_ID, sensorId)) {
    return false;
  }

  Serial.print("TCS34725 ID: 0x");
  Serial.println(sensorId, HEX);

  if (sensorId != 0x44 && sensorId != 0x4D && sensorId != 0x10) {
    return false;
  }

  if (!writeRgbRegister(TCS34725_ATIME, TCS34725_INTEGRATION_154MS)) {
    return false;
  }

  if (!writeRgbRegister(TCS34725_CONTROL, TCS34725_GAIN_16X)) {
    return false;
  }

  if (!writeRgbRegister(TCS34725_ENABLE, TCS34725_ENABLE_PON)) {
    return false;
  }

  delay(3);

  if (!writeRgbRegister(
          TCS34725_ENABLE, TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN)) {
    return false;
  }

  delay(160);
  return true;
}

bool readRgbSensor(RgbReading& reading) {
  return readRgbRegister16(TCS34725_RDATAL, reading.red) &&
         readRgbRegister16(TCS34725_GDATAL, reading.green) &&
         readRgbRegister16(TCS34725_BDATAL, reading.blue) &&
         readRgbRegister16(TCS34725_CDATAL, reading.clear);
}

COLOUR classifyColour(const RgbReading& reading) {
  const float total =
      static_cast<float>(reading.red + reading.green + reading.blue);
  if (reading.clear < 5 || total < 10.0F) {
    return BLACK;
  }

  const float redRatio = reading.red / total;
  const float greenRatio = reading.green / total;
  const float blueRatio = reading.blue / total;
  const uint16_t dominant =
      max(reading.red, max(reading.green, reading.blue));
  const uint16_t weakest =
      min(reading.red, min(reading.green, reading.blue));
  const uint16_t chroma = dominant - weakest;
  const float chromaRatio =
      dominant > 0 ? static_cast<float>(chroma) / dominant : 1.0F;

  // Dark blue often has a low clear value, so classify it before the
  // general black fallback if blue is still clearly dominant.
  if (reading.blue > 35 && blueRatio > 0.38F &&
      reading.blue > reading.red * 1.18F &&
      reading.blue > reading.green * 1.08F && chroma > 12) {
    return BLUE;
  }

  if (reading.clear > 18000 && total > 20000.0F && chromaRatio < 0.55F && redRatio > 0.22F &&
      redRatio < 0.48F && greenRatio > 0.22F && greenRatio < 0.44F &&
      blueRatio > 0.14F && blueRatio < 0.32F) {
    return WHITE;
  }

  if (reading.red > 20 && reading.green > 20 && blueRatio < 0.16F &&
      redRatio > 0.32F && greenRatio > 0.28F &&
      fabsf(redRatio - greenRatio) < 0.18F) {
    return YELLOW;
  }

  if (redRatio > 0.46F && greenRatio < 0.33F &&
      reading.red > reading.green * 1.22F &&
      reading.red > reading.blue * 1.25F) {
    return RED;
  }

  if (greenRatio > 0.38F && reading.green > reading.red * 1.08F &&
      reading.green > reading.blue * 1.18F && blueRatio < 0.34F) {
    return GREEN;
  }

  if (reading.blue > 18 && blueRatio > 0.34F &&
      reading.blue > reading.red * 1.08F &&
      reading.blue > reading.green * 1.04F && chroma > 8) {
    return BLUE;
  }

  if (reading.clear < 7000 || total < 7000.0F ||
      (reading.clear < 12000 && total < 12000.0F && chromaRatio < 0.55F)) {
    return BLACK;
  }

  if (reading.clear > 26000 && total > 28000.0F && chromaRatio < 0.5F &&
      blueRatio > 0.16F && blueRatio < 0.36F &&
      redRatio > 0.24F && redRatio < 0.45F &&
      greenRatio > 0.24F && greenRatio < 0.45F) {
    return WHITE;
  }

  return BLACK;
}

const char* colourToString(COLOUR colour) {
  switch (colour) {
    case BLACK:
      return "black";
    case WHITE:
      return "white";
    case BLUE:
      return "blue";
    case RED:
      return "red";
    case GREEN:
      return "green";
    case YELLOW:
      return "yellow";
    default:
      return "unknown";
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(RGB_SDA_PIN, RGB_SCL_PIN);
  rgbSensorReady = initializeRgbSensor();

  if (rgbSensorReady) {
    Serial.println("RGB sensor ready");
  } else {
    Serial.println("RGB sensor not detected");
  }
}

void loop() {
  if (millis() - lastReadAt < READ_INTERVAL_MS) {
    delay(10);
    return;
  }

  lastReadAt = millis();

  if (!rgbSensorReady) {
    Serial.println("Waiting for TCS34725");
    delay(250);
    return;
  }

  RgbReading reading = {};
  if (!readRgbSensor(reading)) {
    Serial.println("RGB read failed");
    delay(250);
    return;
  }

  const COLOUR detected = classifyColour(reading);
  const float total =
      static_cast<float>(reading.red + reading.green + reading.blue);
  const float redRatio = total > 0.0F ? reading.red / total : 0.0F;
  const float greenRatio = total > 0.0F ? reading.green / total : 0.0F;
  const float blueRatio = total > 0.0F ? reading.blue / total : 0.0F;
  const uint16_t dominant =
      max(reading.red, max(reading.green, reading.blue));
  const uint16_t weakest =
      min(reading.red, min(reading.green, reading.blue));
  const uint16_t chroma = dominant - weakest;
  const float chromaRatio =
      dominant > 0 ? static_cast<float>(chroma) / dominant : 1.0F;

  Serial.print("clear=");
  Serial.print(reading.clear);
  Serial.print(" red=");
  Serial.print(reading.red);
  Serial.print(" green=");
  Serial.print(reading.green);
  Serial.print(" blue=");
  Serial.print(reading.blue);
  Serial.print(" rRatio=");
  Serial.print(redRatio, 3);
  Serial.print(" gRatio=");
  Serial.print(greenRatio, 3);
  Serial.print(" bRatio=");
  Serial.print(blueRatio, 3);
  Serial.print(" chroma=");
  Serial.print(chroma);
  Serial.print(" chromaRatio=");
  Serial.print(chromaRatio, 3);
  Serial.print(" detected=");
  Serial.println(colourToString(detected));
}
