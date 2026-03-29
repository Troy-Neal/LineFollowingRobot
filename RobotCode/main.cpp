#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace {
constexpr char WIFI_SSID[] = "YOUR_HOME_WIFI";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
constexpr char LED_API_URL[] = "https://your-railway-app.up.railway.app/api/led";
constexpr char HEARTBEAT_API_URL[] =
    "https://your-railway-app.up.railway.app/api/device/heartbeat";
constexpr int LED_PIN = 2;
constexpr unsigned long POLL_INTERVAL_MS = 3000;

String desiredState = "off";
bool ledIsOn = false;
unsigned long lastPollAt = 0;

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

void postHeartbeat() {
  HTTPClient http;
  http.begin(HEARTBEAT_API_URL);
  http.addHeader("Content-Type", "application/json");

  String body = "{\"state\":\"";
  body += ledIsOn ? "on" : "off";
  body += "\"}";

  const int statusCode = http.POST(body);
  if (statusCode < 0) {
    Serial.print("Heartbeat failed: ");
    Serial.println(http.errorToString(statusCode));
  }

  http.end();
}

void syncDesiredState() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWifi();
  }

  HTTPClient http;
  http.begin(LED_API_URL);
  const int statusCode = http.GET();

  if (statusCode <= 0) {
    Serial.print("Poll failed: ");
    Serial.println(http.errorToString(statusCode));
    http.end();
    return;
  }

  const String payload = http.getString();
  http.end();

  String nextState;
  if (!extractState(payload, "desiredState", nextState)) {
    Serial.println("Could not parse desiredState");
    return;
  }

  if (nextState != desiredState) {
    desiredState = nextState;
    applyLedState(desiredState);
    Serial.print("LED updated to ");
    Serial.println(desiredState);
  }

  postHeartbeat();
}
}  // namespace

void setup() {
  pinMode(LED_PIN, OUTPUT);
  applyLedState(desiredState);

  Serial.begin(115200);
  delay(200);

  connectToWifi();
  syncDesiredState();
}

void loop() {
  if (millis() - lastPollAt >= POLL_INTERVAL_MS) {
    lastPollAt = millis();
    syncDesiredState();
  }
}
