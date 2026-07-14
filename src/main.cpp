#include <Arduino.h>

#include "config_store.h"
#include "led_status.h"
#include "lock_server.h"
#include "network_manager.h"

namespace {

constexpr uint8_t kFactoryResetPin = 0;  // Botón BOOT integrado.
constexpr unsigned long kFactoryResetHoldMs = 5000;

DeviceConfig deviceConfig;
bool lockServerStarted = false;

// Estado del botón mantenido por interrupción de hardware (no por polling):
// el firmware pasa largos tramos bloqueado dentro de WiFiManager (hasta
// ~12s intentando conectar), y durante esos tramos loop() no corre. Un
// chequeo por polling perdería o mediría mal un mantenido que empiece ahí.
// El ISR solo registra el instante del flanco de bajada; loop() sigue
// comparando contra millis() de forma no bloqueante, como antes.
volatile unsigned long resetPressStartMs = 0;
volatile bool resetButtonDown = false;

void IRAM_ATTR onResetButtonChange() {
  if (digitalRead(kFactoryResetPin) == LOW) {
    resetPressStartMs = millis();
    resetButtonDown = true;
  } else {
    resetButtonDown = false;
  }
}

void checkFactoryResetButton() {
  if (resetButtonDown && (millis() - resetPressStartMs >= kFactoryResetHoldMs)) {
    Serial.println("[FactoryReset] Borrando credenciales y configuracion...");
    Serial.flush();
    NetworkManager::resetWifiCredentials();
    ConfigStore::clear();
    ESP.restart();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(3000);  // Esperamos a que se monte el puerto USB serial virtual

  Serial.println("\n--- Cerradura Inteligente ESP32-S3 ---");

  pinMode(kFactoryResetPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kFactoryResetPin), onResetButtonChange, CHANGE);

  deviceConfig = ConfigStore::load();

  LedStatus::begin();
  NetworkManager::begin(deviceConfig);
}

void loop() {
  NetworkManager::loop();
  LedStatus::update(NetworkManager::state(), LockServer::isRelayActive());
  checkFactoryResetButton();

  if (NetworkManager::isReady()) {
    if (!lockServerStarted) {
      LockServer::begin(deviceConfig);
      lockServerStarted = true;
      Serial.println("[LockServer] Listo en el puerto 80 (endpoint /unlock).");
    }
    LockServer::handleClient();
    LockServer::update();
  }
}
