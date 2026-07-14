#pragma once

#include "config_store.h"
#include "led_status.h"

namespace NetworkManager {

// Arranca la conexión Wi-Fi. Si hay credenciales guardadas intenta conectar
// (bloqueante, acotado por un timeout corto, una única vez al arrancar). Si
// falla o no hay credenciales, levanta el portal cautivo no bloqueante.
// Si forceConfigPortal es true (puente de "modo configuración" presente en
// el arranque), se omite el intento de conexión y se levanta el portal
// directamente, sin tocar las credenciales/config ya guardadas. Debe
// llamarse una sola vez desde setup().
void begin(DeviceConfig &config, bool forceConfigPortal = false);

// Debe llamarse en cada iteración de loop(). Sirve el portal cautivo
// mientras está activo y, una vez conectado, vigila la conexión y reintenta
// si se cae. No bloquea (salvo un breve intento de reconexión acotado).
void loop();

// Estado actual de conectividad, usado para pintar el LED.
NetState state();

// true cuando el dispositivo está conectado a la red STA y el AP quedó apagado.
bool isReady();

// Borra las credenciales Wi-Fi guardadas (usado por el factory reset físico).
void resetWifiCredentials();

}  // namespace NetworkManager
