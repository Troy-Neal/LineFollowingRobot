#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

namespace {
constexpr char WIFI_SSID[] = "TNMG";
constexpr char WIFI_PASSWORD[] = "ForEver0902";
constexpr char WS_HOST[] = "linefollowingrobot.up.railway.app";
constexpr uint16_t WS_PORT = 443;
constexpr char WS_PATH[] = "/ws";
constexpr int LED_PIN = 2;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 1000;
constexpr unsigned long RECONNECT_INTERVAL_MS = 2000;

String desiredState = "off";
bool ledIsOn = false;
unsigned long lastHeartbeatAt = 0;
WebSocketsClient webSocket;
bool websocketReady = false;

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
