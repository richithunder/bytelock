#pragma once

#include <Arduino.h>

// Modo de asignación de IP para la interfaz STA.
enum IpMode : uint8_t {
  IP_MODE_DHCP = 0,
  IP_MODE_STATIC = 1,
};

// Configuración persistente del dispositivo (almacenada en NVS vía Preferences).
struct DeviceConfig {
  uint8_t ipMode = IP_MODE_DHCP;
  String staticIP = "";
  String subnet = "";
  String gateway = "";
  String accessToken = "";     // Debe tener exactamente 16 caracteres.
  uint32_t pulseMs = 1000;     // Duración del pulso del relé, por defecto 1000 ms.
};

namespace ConfigStore {

// Lee la configuración desde NVS. Si no existe ninguna clave, devuelve los
// valores por defecto de DeviceConfig.
DeviceConfig load();

// Persiste la configuración completa en NVS.
void save(const DeviceConfig &config);

// Borra todas las claves de configuración (usado por el factory reset).
void clear();

}  // namespace ConfigStore
