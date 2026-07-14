#include <Arduino.h>
#include <Ticker.h>

#include "config_store.h"
#include "led_status.h"
#include "lock_server.h"
#include "network_manager.h"

namespace {

constexpr uint8_t kFactoryResetPin = 0;  // Botón BOOT integrado.
constexpr unsigned long kFactoryResetHoldMs = 5000;
constexpr uint32_t kResetCheckIntervalMs = 200;

DeviceConfig deviceConfig;
bool lockServerStarted = false;

// Estado del botón mantenido por interrupción de hardware (no por polling):
// el firmware pasa largos tramos bloqueado dentro de WiFiManager (hasta
// ~12s intentando conectar, y en la práctica se vio que a veces no vuelve a
// tiempo). El ISR SOLO toca esta variable volatile: nada más — llamar a
// neopixelWrite() (usa el driver RMT, que internamente espera en un
// semáforo) desde un ISR real es inseguro y puede fallar en silencio o
// corromper el estado del RMT, que además comparte con el parpadeo del LED.
volatile unsigned long resetPressStartMs = 0;
volatile bool resetButtonDown = false;

// El chequeo "¿ya se mantuvo 5s?" tampoco puede vivir solo en loop(): si
// NetworkManager::loop() está trabado dentro de una llamada bloqueante de
// WiFiManager, loop() completo deja de correr y nunca llegaría a evaluarlo.
// Por eso corre en su propio Ticker (contexto de tarea normal, seguro para
// tocar el LED), independiente del loop principal.
Ticker resetCheckTicker;

void IRAM_ATTR onResetButtonChange() {
  if (digitalRead(kFactoryResetPin) == LOW) {
    resetPressStartMs = millis();
    resetButtonDown = true;
  } else {
    resetButtonDown = false;
  }
}

void checkFactoryResetButton() {
  static bool wasDown = false;
  bool isDown = resetButtonDown;

  if (isDown != wasDown) {
    Serial.println(isDown ? "[FactoryReset] Boton presionado (interrupcion detectada)."
                           : "[FactoryReset] Boton liberado.");
    wasDown = isDown;
  }

  if (isDown) {
    // Magenta mientras se mantiene presionado: confirma visualmente que la
    // interrupción detectó el press, sin importar qué esté haciendo el
    // resto del firmware.
    neopixelWrite(RGB_BUILTIN, 255, 0, 255);
  }

  if (isDown && (millis() - resetPressStartMs >= kFactoryResetHoldMs)) {
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
  resetCheckTicker.attach_ms(kResetCheckIntervalMs, checkFactoryResetButton);

  deviceConfig = ConfigStore::load();

  LedStatus::begin();
  NetworkManager::begin(deviceConfig);
}

void loop() {
  NetworkManager::loop();
  LedStatus::update(NetworkManager::state(), LockServer::isRelayActive());
  // El chequeo del factory reset ya no se llama desde acá: corre en
  // resetCheckTicker (ver setup()), para seguir funcionando aunque
  // NetworkManager::loop() esté bloqueado dentro de WiFiManager.

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
