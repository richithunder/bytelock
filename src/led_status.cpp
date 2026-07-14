#include "led_status.h"

#include <Arduino.h>
#include <Ticker.h>

namespace {
constexpr uint32_t kBlinkPeriodMs = 400;
constexpr uint32_t kConnectingBlinkMs = 120;
constexpr uint8_t kBrightness = 255;

Ticker connectingTicker;
bool connectingBlinkOn = false;

void writeColor(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(RGB_BUILTIN, r, g, b);
}

void writeOff() {
  writeColor(0, 0, 0);
}

// Corre en la tarea del Ticker (esp_timer), no en loop(): sigue
// ejecutándose aunque el firmware esté bloqueado dentro de connectWifi().
void toggleConnectingBlink() {
  connectingBlinkOn = !connectingBlinkOn;
  connectingBlinkOn ? writeColor(kBrightness, kBrightness, 0) : writeOff();
}
}  // namespace

namespace LedStatus {

void begin() {
  pinMode(RGB_BUILTIN, OUTPUT);
  writeOff();
}

void update(NetState netState, bool relayActive) {
  if (relayActive) {
    // Pulso de apertura en curso: blanco fijo mientras dure el pulso.
    writeColor(kBrightness, kBrightness, kBrightness);
    return;
  }

  const bool blinkOn = (millis() / kBlinkPeriodMs) % 2 == 0;

  switch (netState) {
    case NetState::AP_CONFIG:
      blinkOn ? writeColor(0, 0, kBrightness) : writeOff();
      break;
    case NetState::CONNECTING:
      // Sin efecto acá: mientras se intenta conectar, el parpadeo amarillo
      // lo maneja beginConnectingBlink()/Ticker, no update() (que no llega
      // a correr durante esas ventanas bloqueantes de WiFiManager).
      break;
    case NetState::CONNECTED:
      writeColor(0, kBrightness, 0);
      break;
    case NetState::DISCONNECTED:
      blinkOn ? writeColor(kBrightness, 0, 0) : writeOff();
      break;
  }
}

void beginConnectingBlink() {
  connectingBlinkOn = false;
  connectingTicker.attach_ms(kConnectingBlinkMs, toggleConnectingBlink);
}

void endConnectingBlink() {
  connectingTicker.detach();
}

}  // namespace LedStatus
