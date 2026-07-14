#include "lock_server.h"

#include <ArduinoJson.h>
#include <WebServer.h>

namespace {

constexpr uint8_t kRelayPin = 5;
// Muchos módulos de relé de un canal para hobby son activos en HIGH; los
// módulos opto-aislados suelen ser activos en LOW. Ajustar aquí según el
// módulo real conectado (ver README, sección Pinout).
constexpr uint8_t kRelayActiveLevel = HIGH;
constexpr uint8_t kRelayInactiveLevel = (kRelayActiveLevel == HIGH) ? LOW : HIGH;

// Buzzer pasivo de pruebas (opcional, no forma parte de la lógica de la
// cerradura): suena en paralelo con cada pulso de apertura para dar
// feedback audible durante el desarrollo/pruebas de banco.
constexpr uint8_t kBuzzerPin = 4;
constexpr uint16_t kBuzzerFrequencyHz = 2000;
constexpr uint16_t kBuzzerDurationMs = 150;

WebServer server(80);
String storedToken;
uint32_t pulseMs = 1000;

bool relayActive = false;
unsigned long relayOffAtMs = 0;

void setRelay(bool active) {
  digitalWrite(kRelayPin, active ? kRelayActiveLevel : kRelayInactiveLevel);
  relayActive = active;
}

// Comparación en tiempo constante para no filtrar, vía el tiempo de
// respuesta, en qué posición difiere el token recibido del almacenado.
bool tokensMatch(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); ++i) {
    diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
  }
  return diff == 0;
}

// Precedencia: header X-Access-Token > query param "token" > body JSON {"token": "..."}.
String extractToken() {
  if (server.hasHeader("X-Access-Token")) {
    return server.header("X-Access-Token");
  }
  if (server.hasArg("token")) {
    return server.arg("token");
  }
  if (server.hasArg("plain")) {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok) {
      const char *t = doc["token"] | "";
      if (t[0] != '\0') return String(t);
    }
  }
  return "";
}

void sendJson(int statusCode, const char *status, const char *message) {
  JsonDocument doc;
  doc["status"] = status;
  doc["message"] = message;
  String body;
  serializeJson(doc, body);
  server.send(statusCode, "application/json", body);
}

void handleUnlock() {
  String token = extractToken();

  if (token.length() != 16 || !tokensMatch(token, storedToken)) {
    sendJson(401, "error", "unauthorized");
    return;
  }

  setRelay(true);
  relayOffAtMs = millis() + pulseMs;
  tone(kBuzzerPin, kBuzzerFrequencyHz, kBuzzerDurationMs);  // No bloqueante.
  sendJson(200, "ok", "unlocked");
}

void handleNotFound() {
  sendJson(404, "error", "not_found");
}

}  // namespace

namespace LockServer {

void begin(const DeviceConfig &config) {
  storedToken = config.accessToken;
  pulseMs = config.pulseMs;

  pinMode(kRelayPin, OUTPUT);
  setRelay(false);

  static const char *kCollectedHeaders[] = {"X-Access-Token"};
  server.collectHeaders(kCollectedHeaders, 1);

  server.on("/unlock", HTTP_GET, handleUnlock);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleClient() {
  server.handleClient();
}

void update() {
  if (relayActive && millis() >= relayOffAtMs) {
    setRelay(false);
  }
}

bool isRelayActive() {
  return relayActive;
}

}  // namespace LockServer
