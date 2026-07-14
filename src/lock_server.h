#pragma once

#include "config_store.h"

namespace LockServer {

// Configura el pin del relé, registra las rutas HTTP y arranca el servidor
// (puerto 80). Debe llamarse una única vez, cuando NetworkManager::isReady().
void begin(const DeviceConfig &config);

// Atiende clientes HTTP pendientes. Debe llamarse en cada iteración de loop().
void handleClient();

// Revisa si el pulso del relé debe terminar (millis(), sin delay()). Debe
// llamarse en cada iteración de loop().
void update();

// true mientras el relé está energizado (pulso de apertura en curso).
bool isRelayActive();

}  // namespace LockServer
