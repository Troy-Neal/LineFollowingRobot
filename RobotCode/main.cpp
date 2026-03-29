#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>

namespace {
using namespace websockets;

constexpr char WIFI_SSID[] = "YOUR_HOME_WIFI";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
constexpr char WS_URL[] = "wss://your-railway-app.up.railway.app/ws";
constexpr int LED_PIN = 2;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 1000;
constexpr unsigned long RECONNECT_INTERVAL_MS = 2000;

String desiredState = "off";
bool ledIsOn = false;
unsigned long lastHeartbeatAt = 0;
unsigned long lastReconnectAttemptAt = 0;
WebsocketsClient websocket;
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
  websocket.send("{\"type\":\"hello\",\"role\":\"device\"}");
}

void sendHeartbeat() {
  if (!websocket.available()) {
    return;
  }

  String payload = "{\"type\":\"device-state\",\"state\":\"";
  payload += ledIsOn ? "on" : "off";
  payload += "\"}";
  websocket.send(payload);
}

void handleMessage(WebsocketsMessage message) {
  const String payload = message.data();
  String nextState;

  if (extractState(payload, "desiredState", nextState) && nextState != desiredState) {
    desiredState = nextState;
    applyLedState(desiredState);
    Serial.print("LED updated to ");
    Serial.println(desiredState);
  }
}

void handleEvent(WebsocketsEvent event, String) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    websocketReady = true;
    Serial.println("WebSocket connected");
    sendHello();
    sendHeartbeat();
  }

  if (event == WebsocketsEvent::ConnectionClosed) {
    websocketReady = false;
    Serial.println("WebSocket disconnected");
  }
}

void connectWebSocket() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWifi();
  }

  Serial.println("Connecting to Railway WebSocket");
  websocket.onMessage(handleMessage);
  websocket.onEvent(handleEvent);
  websocket.setInsecure();
  websocket.setReconnectInterval(RECONNECT_INTERVAL_MS);

  if (!websocket.connect(WS_URL)) {
    Serial.println("WebSocket connect failed");
    websocketReady = false;
  }
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

  if (!websocket.available()) {
    const unsigned long now = millis();

    if (now - lastReconnectAttemptAt >= RECONNECT_INTERVAL_MS) {
      lastReconnectAttemptAt = now;
      connectWebSocket();
    }

    delay(10);
    return;
  }

  websocket.poll();

  if (websocketReady && millis() - lastHeartbeatAt >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatAt = millis();
    sendHeartbeat();
  }
}
