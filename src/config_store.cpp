#include "config_store.h"

#include <Preferences.h>

namespace {
constexpr char kNamespace[] = "lockcfg";

// Preferences::getString(key, default) registra un log de nivel ERROR
// cuando la clave todavía no existe (a diferencia de los getters numéricos,
// que solo registran a nivel verbose). Se evita ese ruido de log
// consultando isKey() primero, en vez de dejar que falle silenciosamente
// puertas adentro.
String getStringOrDefault(Preferences &prefs, const char *key, const char *defaultValue) {
  return prefs.isKey(key) ? prefs.getString(key, defaultValue) : String(defaultValue);
}
}  // namespace

namespace ConfigStore {

DeviceConfig load() {
  Preferences prefs;
  DeviceConfig config;

  // Se abre en modo escritura (no solo lectura): si el namespace todavía no
  // existe (primer arranque, antes de cualquier save()), abrir en readOnly
  // falla con nvs_open()==NOT_FOUND y ensucia el log con un [E] en cada
  // boot, aunque los getters de todas formas devuelven sus valores por
  // defecto de forma segura. Abrir en modo escritura crea el namespace
  // vacío la primera vez, sin ese ruido, y load() nunca escribe nada.
  prefs.begin(kNamespace, /*readOnly=*/false);
  config.ipMode = prefs.getUChar("ipMode", IP_MODE_DHCP);
  config.staticIP = getStringOrDefault(prefs, "staticIP", "");
  config.subnet = getStringOrDefault(prefs, "subnet", "");
  config.gateway = getStringOrDefault(prefs, "gateway", "");
  config.accessToken = getStringOrDefault(prefs, "accessToken", "");
  config.pulseMs = prefs.getUInt("pulseMs", 1000);
  prefs.end();

  return config;
}

void save(const DeviceConfig &config) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putUChar("ipMode", config.ipMode);
  prefs.putString("staticIP", config.staticIP);
  prefs.putString("subnet", config.subnet);
  prefs.putString("gateway", config.gateway);
  prefs.putString("accessToken", config.accessToken);
  prefs.putUInt("pulseMs", config.pulseMs);
  prefs.end();
}

void clear() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.clear();
  prefs.end();
}

}  // namespace ConfigStore
