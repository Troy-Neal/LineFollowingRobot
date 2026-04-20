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
constexpr unsigned long LINE_FOLLOW_INTERVAL_MS = 20;

// Front line following sensors.
constexpr int FF_LINE_PIN = 35;
constexpr int FS_LINE_PIN = 32;
constexpr int FT_LINE_PIN = 34;

// Back line following sensors.
constexpr int BF_LINE_PIN = 33;
constexpr int BS_LINE_PIN = 19;
constexpr int BT_LINE_PIN = 18;

// Motor driver pins.
constexpr int LEFT_MOTOR_EN_PIN = 25;
constexpr int LEFT_MOTOR_IN1_PIN = 26;
constexpr int LEFT_MOTOR_IN2_PIN = 27;
constexpr int RIGHT_MOTOR_IN1_PIN = 14;
constexpr int RIGHT_MOTOR_IN2_PIN = 23;
constexpr int RIGHT_MOTOR_EN_PIN = 17;
constexpr int LEFT_MOTOR_PWM_CHANNEL = 0;
constexpr int RIGHT_MOTOR_PWM_CHANNEL = 1;
constexpr int MOTOR_PWM_FREQUENCY = 1000;
constexpr int MOTOR_PWM_RESOLUTION_BITS = 8;
constexpr int DEFAULT_MOTOR_SPEED = 180;
constexpr bool LEFT_MOTOR_INVERTED = false;
constexpr bool RIGHT_MOTOR_INVERTED = true;

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
constexpr uint8_t TCS34725_GAIN_16X = 0x02;

// Only one ultrasonic sensor is currently connected, and it faces forward.
constexpr int FRONT_DIST_TRIG_PIN = 13;
constexpr int FRONT_DIST_ECHO_PIN = 15;

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

struct MotorCommand {
  int leftSpeed;
  int rightSpeed;
};

enum DriveMode {
  DRIVE_MANUAL,
  DRIVE_LINE_FOLLOW_WAITING,
  DRIVE_LINE_FOLLOW_ACTIVE,
};

String desiredState = "off";
bool ledIsOn = false;
unsigned long lastHeartbeatAt = 0;
WebSocketsClient webSocket;
bool websocketReady = false;
COLOUR desiredColour = BLUE;
bool rgbSensorReady = false;
MotorCommand currentMotorCommand = {0, 0};
DriveMode currentDriveMode = DRIVE_MANUAL;
unsigned long lastLineFollowAt = 0;
int lastLineFollowTurn = 0;

String escapeJsonString(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const char current = value[i];
    switch (current) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += current;
        break;
    }
  }

  return escaped;
}

void sendRobotLog(const char* level, const String& message) {
  Serial.print("[");
  Serial.print(level);
  Serial.print("] ");
  Serial.println(message);

  if (!webSocket.isConnected()) {
    return;
  }

  String payload = "{\"type\":\"robot-log\",\"level\":\"";
  payload += level;
  payload += "\",\"message\":\"";
  payload += escapeJsonString(message);
  payload += "\"}";
  webSocket.sendTXT(payload);
}

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

bool extractStringValue(const String& payload, const char* key, String& result) {
  return extractState(payload, key, result);
}

bool extractIntValue(const String& payload, const char* key, int& result) {
  String marker = "\"";
  marker += key;
  marker += "\":";

  const int markerIndex = payload.indexOf(marker);
  if (markerIndex < 0) {
    return false;
  }

  int valueStart = markerIndex + marker.length();
  while (valueStart < payload.length() && payload[valueStart] == ' ') {
    ++valueStart;
  }

  bool quoted = false;
  if (valueStart < payload.length() && payload[valueStart] == '"') {
    quoted = true;
    ++valueStart;
  }

  int valueEnd = valueStart;
  while (valueEnd < payload.length()) {
    const char current = payload[valueEnd];
    if (quoted) {
      if (current == '"') {
        break;
      }
    } else if (current == ',' || current == '}' || current == ' ') {
      break;
    }
    ++valueEnd;
  }

  if (valueEnd <= valueStart) {
    return false;
  }

  result = payload.substring(valueStart, valueEnd).toInt();
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

void initializeMotors() {
  pinMode(LEFT_MOTOR_IN1_PIN, OUTPUT);
  pinMode(LEFT_MOTOR_IN2_PIN, OUTPUT);
  pinMode(RIGHT_MOTOR_IN1_PIN, OUTPUT);
  pinMode(RIGHT_MOTOR_IN2_PIN, OUTPUT);

  ledcSetup(LEFT_MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQUENCY,
            MOTOR_PWM_RESOLUTION_BITS);
  ledcSetup(RIGHT_MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQUENCY,
            MOTOR_PWM_RESOLUTION_BITS);
  ledcAttachPin(LEFT_MOTOR_EN_PIN, LEFT_MOTOR_PWM_CHANNEL);
  ledcAttachPin(RIGHT_MOTOR_EN_PIN, RIGHT_MOTOR_PWM_CHANNEL);
}

void applySingleMotor(int in1Pin, int in2Pin, int pwmChannel, int speed,
                      bool inverted) {
  const int clippedSpeed = constrain(speed, -255, 255);
  if (clippedSpeed == 0) {
    digitalWrite(in1Pin, LOW);
    digitalWrite(in2Pin, LOW);
    ledcWrite(pwmChannel, 0);
    return;
  }

  const bool forward = inverted ? clippedSpeed < 0 : clippedSpeed > 0;
  digitalWrite(in1Pin, forward ? HIGH : LOW);
  digitalWrite(in2Pin, forward ? LOW : HIGH);
  ledcWrite(pwmChannel, abs(clippedSpeed));
}

void applyMotorCommand(int leftSpeed, int rightSpeed) {
  currentMotorCommand.leftSpeed = constrain(leftSpeed, -255, 255);
  currentMotorCommand.rightSpeed = constrain(rightSpeed, -255, 255);

  applySingleMotor(LEFT_MOTOR_IN1_PIN, LEFT_MOTOR_IN2_PIN,
                   LEFT_MOTOR_PWM_CHANNEL, currentMotorCommand.leftSpeed,
                   LEFT_MOTOR_INVERTED);
  applySingleMotor(RIGHT_MOTOR_IN1_PIN, RIGHT_MOTOR_IN2_PIN,
                   RIGHT_MOTOR_PWM_CHANNEL, currentMotorCommand.rightSpeed,
                   RIGHT_MOTOR_INVERTED);
}

void stopMotors() {
  applyMotorCommand(0, 0);
}

const char* driveModeToString(DriveMode mode) {
  switch (mode) {
    case DRIVE_LINE_FOLLOW_WAITING:
      return "line_follow_waiting";
    case DRIVE_LINE_FOLLOW_ACTIVE:
      return "line_follow_active";
    case DRIVE_MANUAL:
    default:
      return "manual";
  }
}

bool sensorSeesLine(int reading) {
  return reading == LOW;
}

void startLineFollowMode() {
  currentDriveMode = DRIVE_LINE_FOLLOW_WAITING;
  lastLineFollowTurn = 0;
  stopMotors();
  sendRobotLog("info", "Line follow requested. Put the robot onto the line.");
}

void stopLineFollowMode() {
  if (currentDriveMode != DRIVE_MANUAL) {
    sendRobotLog("info", "Line follow stopped.");
  }
  currentDriveMode = DRIVE_MANUAL;
  lastLineFollowTurn = 0;
  stopMotors();
}

void updateLineFollow() {
  if (currentDriveMode == DRIVE_MANUAL) {
    return;
  }

  if (millis() - lastLineFollowAt < LINE_FOLLOW_INTERVAL_MS) {
    return;
  }

  lastLineFollowAt = millis();

  const LineSensorReadings readings = readLineSensors();
  const bool leftOnLine = sensorSeesLine(readings.frontFar);
  const bool centreOnLine = sensorSeesLine(readings.frontSide);
  const bool rightOnLine = sensorSeesLine(readings.frontThird);
  const bool anyOnLine = leftOnLine || centreOnLine || rightOnLine;
  constexpr int FORWARD_SPEED = 150;
  constexpr int TURN_SPEED = 140;
  constexpr int SEARCH_SPEED = 110;

  if (!anyOnLine) {
    if (currentDriveMode == DRIVE_LINE_FOLLOW_WAITING) {
      stopMotors();
      return;
    }

    if (lastLineFollowTurn < 0) {
      applyMotorCommand(SEARCH_SPEED / 2, SEARCH_SPEED);
    } else if (lastLineFollowTurn > 0) {
      applyMotorCommand(SEARCH_SPEED, SEARCH_SPEED / 2);
    } else {
      stopMotors();
    }
    return;
  }

  if (currentDriveMode == DRIVE_LINE_FOLLOW_WAITING) {
    currentDriveMode = DRIVE_LINE_FOLLOW_ACTIVE;
    sendRobotLog("info", "Line acquired. Following started.");
  }

  if (centreOnLine && !leftOnLine && !rightOnLine) {
    lastLineFollowTurn = 0;
    applyMotorCommand(FORWARD_SPEED, FORWARD_SPEED);
    return;
  }

  if (leftOnLine && !rightOnLine) {
    lastLineFollowTurn = -1;
    applyMotorCommand(TURN_SPEED / 3, TURN_SPEED);
    return;
  }

  if (rightOnLine && !leftOnLine) {
    lastLineFollowTurn = 1;
    applyMotorCommand(TURN_SPEED, TURN_SPEED / 3);
    return;
  }

  lastLineFollowTurn = 0;
  applyMotorCommand(FORWARD_SPEED, FORWARD_SPEED);
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
  return readUltrasonicDistanceCm(FRONT_DIST_TRIG_PIN, FRONT_DIST_ECHO_PIN);
}

float readBackDistanceCm() {
  return -1.0F;
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

COLOUR classifyColour(uint16_t red, uint16_t green, uint16_t blue,
                      uint16_t clear) {
  const float total = static_cast<float>(red + green + blue);
  if (clear < 5 || total < 10.0F) {
    return BLACK;
  }

  const float redRatio = red / total;
  const float greenRatio = green / total;
  const float blueRatio = blue / total;
  const uint16_t dominant = max(red, max(green, blue));
  const uint16_t weakest = min(red, min(green, blue));
  const uint16_t chroma = dominant - weakest;
  const float chromaRatio =
      dominant > 0 ? static_cast<float>(chroma) / dominant : 1.0F;

  if (blue > 35 && blueRatio > 0.38F && blue > red * 1.18F &&
      blue > green * 1.08F && chroma > 12) {
    return BLUE;
  }

  if (clear > 18000 && total > 20000.0F && chromaRatio < 0.55F &&
      redRatio > 0.22F && redRatio < 0.48F && greenRatio > 0.22F &&
      greenRatio < 0.44F && blueRatio > 0.14F && blueRatio < 0.32F) {
    return WHITE;
  }

  if (red > 20 && green > 20 && blueRatio < 0.16F && redRatio > 0.32F &&
      greenRatio > 0.28F && fabsf(redRatio - greenRatio) < 0.18F) {
    return YELLOW;
  }

  if (redRatio > 0.46F && greenRatio < 0.33F && red > green * 1.22F &&
      red > blue * 1.25F) {
    return RED;
  }

  if (greenRatio > 0.38F && green > red * 1.08F && green > blue * 1.18F &&
      blueRatio < 0.34F) {
    return GREEN;
  }

  if (blue > 18 && blueRatio > 0.34F && blue > red * 1.08F &&
      blue > green * 1.04F && chroma > 8) {
    return BLUE;
  }

  if (clear < 7000 || total < 7000.0F ||
      (clear < 12000 && total < 12000.0F && chromaRatio < 0.55F)) {
    return BLACK;
  }

  if (clear > 26000 && total > 28000.0F && chromaRatio < 0.5F &&
      blueRatio > 0.16F && blueRatio < 0.36F && redRatio > 0.24F &&
      redRatio < 0.45F && greenRatio > 0.24F && greenRatio < 0.45F) {
    return WHITE;
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
  initializeMotors();
  initializeUltrasonicSensor(FRONT_DIST_TRIG_PIN, FRONT_DIST_ECHO_PIN);
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
  sendRobotLog("info", "Wi-Fi connected: " + WiFi.localIP().toString());
}

void sendHello() {
  webSocket.sendTXT("{\"type\":\"hello\",\"role\":\"device\"}");
}

void sendHeartbeat() {
  if (!webSocket.isConnected()) {
    return;
  }

  const LineSensorReadings lineReadings = readLineSensors();
  const UltrasonicReadings ultrasonicReadings = readUltrasonicSensors();
  const COLOUR detectedColour = readDetectedColour();

  String payload = "{\"type\":\"device-state\",\"state\":\"";
  payload += ledIsOn ? "on" : "off";
  payload += "\",\"motors\":{\"left\":";
  payload += currentMotorCommand.leftSpeed;
  payload += ",\"right\":";
  payload += currentMotorCommand.rightSpeed;
  payload += ",\"mode\":\"";
  payload += driveModeToString(currentDriveMode);
  payload += "\"";
  payload += "},\"line\":{\"ff\":";
  payload += lineReadings.frontFar;
  payload += ",\"fs\":";
  payload += lineReadings.frontSide;
  payload += ",\"ft\":";
  payload += lineReadings.frontThird;
  payload += ",\"bf\":";
  payload += lineReadings.backFar;
  payload += ",\"bs\":";
  payload += lineReadings.backSide;
  payload += ",\"bt\":";
  payload += lineReadings.backThird;
  payload += "},\"distance\":{\"frontCm\":";
  payload += String(ultrasonicReadings.frontCm, 2);
  payload += ",\"backCm\":";
  payload += String(ultrasonicReadings.backCm, 2);
  payload += "},\"colour\":\"";
  payload += colourToString(detectedColour);
  payload += "\"}";
  webSocket.sendTXT(payload);
}

void applyNamedMotorCommand(const String& command, int speed) {
  const int clippedSpeed = constrain(speed, 0, 255);

  if (command == "forward") {
    currentDriveMode = DRIVE_MANUAL;
    applyMotorCommand(clippedSpeed, clippedSpeed);
  } else if (command == "backward" || command == "reverse") {
    currentDriveMode = DRIVE_MANUAL;
    applyMotorCommand(-clippedSpeed, -clippedSpeed);
  } else if (command == "left") {
    currentDriveMode = DRIVE_MANUAL;
    applyMotorCommand(-clippedSpeed, clippedSpeed);
  } else if (command == "right") {
    currentDriveMode = DRIVE_MANUAL;
    applyMotorCommand(clippedSpeed, -clippedSpeed);
  } else if (command == "line_follow" || command == "line-follow") {
    startLineFollowMode();
  } else if (command == "stop") {
    stopLineFollowMode();
  }
}

void handleMessage(const String& payload) {
  String messageType;
  extractStringValue(payload, "type", messageType);

  if (messageType == "error") {
    sendRobotLog("error", "Server error: " + payload);
  }

  String nextState;
  String motionCommand;
  int leftSpeed = 0;
  int rightSpeed = 0;
  int speed = DEFAULT_MOTOR_SPEED;

  if (extractState(payload, "desiredState", nextState) && nextState != desiredState) {
    desiredState = nextState;
    applyLedState(desiredState);
    sendRobotLog("info", "LED updated to " + desiredState);
  }

  if (extractIntValue(payload, "speed", speed) ||
      extractIntValue(payload, "motorSpeed", speed)) {
    speed = constrain(speed, 0, 255);
  }

  const bool hasDirectMotorSpeeds =
      extractIntValue(payload, "leftSpeed", leftSpeed) &&
      extractIntValue(payload, "rightSpeed", rightSpeed);

  if (hasDirectMotorSpeeds) {
    currentDriveMode = DRIVE_MANUAL;
    applyMotorCommand(leftSpeed, rightSpeed);
    return;
  }

  int lineFollowEnabled = 0;
  if (extractIntValue(payload, "lineFollowEnabled", lineFollowEnabled)) {
    if (lineFollowEnabled != 0) {
      startLineFollowMode();
    } else {
      stopLineFollowMode();
    }
    return;
  }

  if (extractStringValue(payload, "motorCommand", motionCommand) ||
      extractStringValue(payload, "direction", motionCommand) ||
      extractStringValue(payload, "command", motionCommand) ||
      extractStringValue(payload, "movement", motionCommand)) {
    motionCommand.toLowerCase();
    applyNamedMotorCommand(motionCommand, speed);
  }
}

void handleEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      websocketReady = true;
      sendRobotLog("info", "WebSocket connected");
      sendHello();
      sendRobotLog("info", rgbSensorReady ? "RGB sensor ready"
                                          : "RGB sensor not detected");
      sendRobotLog("info", "Robot online");
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
  stopLineFollowMode();

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
  updateLineFollow();

  if (websocketReady && millis() - lastHeartbeatAt >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatAt = millis();
    sendHeartbeat();
  }

  delay(10);
}
