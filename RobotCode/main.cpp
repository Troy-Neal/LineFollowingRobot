#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Wire.h>

namespace {
constexpr char WIFI_SSID[] = "BELL171";
constexpr char WIFI_PASSWORD[] = "Good601Password";
constexpr char WS_HOST[] = "linefollowingrobot.up.railway.app";
constexpr uint16_t WS_PORT = 443;
constexpr char WS_PATH[] = "/ws";
constexpr int LED_PIN = 2;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 1000;
constexpr unsigned long RECONNECT_INTERVAL_MS = 2000;
constexpr unsigned long ULTRASONIC_TIMEOUT_US = 30000;

// Front line following sensors.
constexpr int FF_LINE_PIN = 35;
constexpr int FS_LINE_PIN = 32;
constexpr int FT_LINE_PIN = 34;

// Back line following sensors.
constexpr int BF_LINE_PIN = 18;
constexpr int BS_LINE_PIN = 19;
constexpr int BT_LINE_PIN = 33;

// RGB sensor I2C pins.
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
constexpr uint8_t TCS34725_GAIN_4X = 0x01;

// Front ultrasonic sensor.
constexpr int F_DIST_TRIG_PIN = 4;
constexpr int F_DIST_ECHO_PIN = 5;

// Back ultrasonic sensor.
constexpr int B_DIST_TRIG_PIN = 13;
constexpr int B_DIST_ECHO_PIN = 15;

enum COLOUR { BLACK, WHITE, BLUE, RED, GREEN, YELLOW };

struct LineSensorReadings {
  int frontFar;
  int frontSide;
  int frontThird;
  int backFar;
  int backSide;
  int backThird;
};

struct UltrasonicReadings {
  float frontCm;
  float backCm;
};

struct RgbReading {
  uint16_t red;
  uint16_t green;
  uint16_t blue;
  uint16_t clear;
};

String desiredState = "off";
bool ledIsOn = false;
unsigned long lastHeartbeatAt = 0;
WebSocketsClient webSocket;
bool websocketReady = false;
COLOUR desiredColour = BLUE;
bool rgbSensorReady = false;

bool extractState(const String& payload, const char* key, String& result) {
  String marker = "\"";
  marker += key;
  marker += "\":\"";

  const int markerIndex = payload.indexOf(marker);
  if (markerIndex < 0) {
    return false;
  }

  const int valueStart = markerIndex + marker.length();
  const int valueEnd = payload.indexOf('"', valueStart);
  if (valueEnd < 0) {
    return false;
  }

  result = payload.substring(valueStart, valueEnd);
  return true;
}

void initializeLineSensors() {
  pinMode(FF_LINE_PIN, INPUT);
  pinMode(FS_LINE_PIN, INPUT);
  pinMode(FT_LINE_PIN, INPUT);
  pinMode(BF_LINE_PIN, INPUT);
  pinMode(BS_LINE_PIN, INPUT);
  pinMode(BT_LINE_PIN, INPUT);
}

int readLineSensor(int pin) {
  return digitalRead(pin);
}

LineSensorReadings readLineSensors() {
  return {
      readLineSensor(FF_LINE_PIN),
      readLineSensor(FS_LINE_PIN),
      readLineSensor(FT_LINE_PIN),
      readLineSensor(BF_LINE_PIN),
      readLineSensor(BS_LINE_PIN),
      readLineSensor(BT_LINE_PIN),
  };
}

void initializeUltrasonicSensor(int trigPin, int echoPin) {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
}

float readUltrasonicDistanceCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  const unsigned long pulseWidth =
      pulseIn(echoPin, HIGH, ULTRASONIC_TIMEOUT_US);

  if (pulseWidth == 0) {
    return -1.0F;
  }

  return static_cast<float>(pulseWidth) * 0.0343F / 2.0F;
}

float readFrontDistanceCm() {
  return readUltrasonicDistanceCm(F_DIST_TRIG_PIN, F_DIST_ECHO_PIN);
}

float readBackDistanceCm() {
  return readUltrasonicDistanceCm(B_DIST_TRIG_PIN, B_DIST_ECHO_PIN);
}

UltrasonicReadings readUltrasonicSensors() {
  return {readFrontDistanceCm(), readBackDistanceCm()};
}

void initializeRgbSensorBus() {
  Wire.begin(RGB_SDA_PIN, RGB_SCL_PIN);
}

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

  if (sensorId != 0x44 && sensorId != 0x4D && sensorId != 0x10) {
    return false;
  }

  if (!writeRgbRegister(TCS34725_ATIME, TCS34725_INTEGRATION_154MS)) {
    return false;
  }

  if (!writeRgbRegister(TCS34725_CONTROL, TCS34725_GAIN_4X)) {
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

COLOUR classifyColour(uint16_t red, uint16_t green, uint16_t blue, uint16_t clear) {
  if (clear < 80) {
    return BLACK;
  }

  if (clear > 900 && red > 300 && green > 300 && blue > 300) {
    return WHITE;
  }

  if (red > green * 1.35F && red > blue * 1.35F) {
    return RED;
  }

  if (green > red * 1.2F && green > blue * 1.2F) {
    return GREEN;
  }

  if (blue > red * 1.2F && blue > green * 1.2F) {
    return BLUE;
  }

  if (red > 220 && green > 220 && blue < 180) {
    return YELLOW;
  }

  return BLACK;
}

COLOUR readDetectedColour() {
  if (!rgbSensorReady) {
    return BLACK;
  }

  RgbReading reading = {};
  if (!readRgbSensor(reading)) {
    return BLACK;
  }

  return classifyColour(reading.red, reading.green, reading.blue, reading.clear);
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

void initializeSensors() {
  initializeLineSensors();
  initializeUltrasonicSensor(F_DIST_TRIG_PIN, F_DIST_ECHO_PIN);
  initializeUltrasonicSensor(B_DIST_TRIG_PIN, B_DIST_ECHO_PIN);
  initializeRgbSensorBus();
  rgbSensorReady = initializeRgbSensor();
}

void applyLedState(const String& state) {
  ledIsOn = state == "on";
  digitalWrite(LED_PIN, ledIsOn ? HIGH : LOW);
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
}

void sendHello() {
  webSocket.sendTXT("{\"type\":\"hello\",\"role\":\"device\"}");
}

void sendHeartbeat() {
  if (!webSocket.isConnected()) {
    return;
  }

  String payload = "{\"type\":\"device-state\",\"state\":\"";
  payload += ledIsOn ? "on" : "off";
  payload += "\"}";
  webSocket.sendTXT(payload);
}

void handleMessage(const String& payload) {
  Serial.print("WS message: ");
  Serial.println(payload);

  String nextState;

  if (extractState(payload, "desiredState", nextState) && nextState != desiredState) {
    desiredState = nextState;
    applyLedState(desiredState);
    Serial.print("LED updated to ");
    Serial.println(desiredState);
  }
}

void handleEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      websocketReady = true;
      Serial.println("WebSocket connected");
      sendHello();
      sendHeartbeat();
      break;
    case WStype_DISCONNECTED:
      websocketReady = false;
      Serial.println("WebSocket disconnected");
      break;
    case WStype_TEXT:
      handleMessage(String(reinterpret_cast<char*>(payload), length));
      break;
    case WStype_ERROR:
      websocketReady = false;
      Serial.println("WebSocket error");
      break;
    default:
      break;
  }
}

void connectWebSocket() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWifi();
  }

  Serial.println("Connecting to Railway WebSocket");
  webSocket.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  webSocket.setReconnectInterval(RECONNECT_INTERVAL_MS);
  webSocket.onEvent(handleEvent);
}
}  // namespace

void setup() {
  pinMode(LED_PIN, OUTPUT);
  applyLedState(desiredState);

  Serial.begin(115200);
  delay(200);

  initializeSensors();

  if (rgbSensorReady) {
    Serial.println("TCS34725 ready");
  } else {
    Serial.println("TCS34725 not detected");
  }

  connectToWifi();
  connectWebSocket();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWifi();
  }

  webSocket.loop();

  if (websocketReady && millis() - lastHeartbeatAt >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatAt = millis();
    sendHeartbeat();
  }

  delay(10);
}
