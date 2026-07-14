#include "network_manager.h"

#include <WiFi.h>
#include <WiFiManager.h>

namespace {

constexpr char kApNamePrefix[] = "byteLock";
constexpr unsigned long kConnectTimeoutSeconds = 8;
constexpr unsigned long kReconnectIntervalMs = 30000;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);
// Margen para que la página de confirmación del portal termine de enviarse
// antes de reiniciar (el reinicio no puede hacerse dentro del propio
// callback: la respuesta HTTP todavía no se envió en ese punto).
constexpr unsigned long kRestartGraceMs = 1500;

WiFiManager wm;
DeviceConfig *activeConfig = nullptr;

NetState currentState = NetState::CONNECTING;
bool apActive = false;
bool staReady = false;
bool saveRequested = false;
unsigned long saveRequestedAtMs = 0;
unsigned long lastReconnectAttemptMs = 0;

// Los parámetros viven durante todo el ciclo de vida del firmware: se crean
// una única vez en begin() y WiFiManager conserva punteros a ellos mientras
// el portal esté activo.
WiFiManagerParameter *macInfoParam = nullptr;
WiFiManagerParameter *ipModeSelectParam = nullptr;
WiFiManagerParameter *staticIpParam = nullptr;
WiFiManagerParameter *subnetParam = nullptr;
WiFiManagerParameter *gatewayParam = nullptr;
WiFiManagerParameter *closeStaticGroupParam = nullptr;
WiFiManagerParameter *tokenParam = nullptr;
WiFiManagerParameter *pulseParam = nullptr;

// Buffers que respaldan el HTML crudo de los parámetros de arriba (deben
// sobrevivir tanto como los WiFiManagerParameter que los referencian), y el
// nombre del AP armado en runtime (wm.autoConnect() se queda con el puntero).
String macInfoHtml;
String ipModeSelectHtml;
String apNameStorage;

// "byteLock" + últimos 4 caracteres hex de la MAC (sin ':'), p.ej.
// MAC 98:A3:16:E5:A0:40 -> "byteLockA040".
String buildApName() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  String suffix = mac.length() >= 4 ? mac.substring(mac.length() - 4) : mac;
  return String(kApNamePrefix) + suffix;
}

const char kHeadScript[] =
    "<style>#staticFieldsGroup{margin:6px 0 12px 0;padding-left:8px;"
    "border-left:2px solid #ccc}</style>"
    "<script>"
    "function toggleStaticFields(){"
    "var sel=document.getElementById('ipMode');"
    "var grp=document.getElementById('staticFieldsGroup');"
    "if(!sel||!grp)return;"
    "grp.style.display=(sel.value==='1')?'block':'none';"
    "}"
    "window.addEventListener('load',toggleStaticFields);"
    "</script>";

String buildMacInfoHtml() {
  String html = "<p><strong>MAC del dispositivo:</strong> ";
  html += WiFi.macAddress();
  html += "</p>";
  return html;
}

// Genera el <select> DHCP/IP Fija y abre el <div> contenedor de los campos
// de IP estática (que se cierra con closeStaticGroupParam). El valor
// seleccionado se lee luego directamente de wm.server->arg("ipMode").
String buildIpModeSelectHtml(uint8_t currentMode) {
  String html = "<label for='ipMode'>Modo de IP</label>";
  html += "<select name='ipMode' id='ipMode' onchange='toggleStaticFields()'>";
  html += "<option value='0'";
  if (currentMode == IP_MODE_DHCP) html += " selected";
  html += ">DHCP</option>";
  html += "<option value='1'";
  if (currentMode == IP_MODE_STATIC) html += " selected";
  html += ">IP Fija</option>";
  html += "</select>";
  html += "<div id='staticFieldsGroup'>";
  return html;
}

String currentIpModeValue() {
  if (wm.server) {
    return wm.server->arg("ipMode");
  }
  return "0";
}

void saveParamsCallback() {
  if (activeConfig == nullptr) return;

  DeviceConfig cfg = *activeConfig;

  cfg.ipMode = (currentIpModeValue() == "1") ? IP_MODE_STATIC : IP_MODE_DHCP;
  cfg.staticIP = String(staticIpParam->getValue());
  cfg.subnet = String(subnetParam->getValue());
  cfg.gateway = String(gatewayParam->getValue());

  String token = String(tokenParam->getValue());
  if (token.length() > 16) token = token.substring(0, 16);
  while (token.length() < 16) token += "0";  // Salvaguarda: se exige longitud 16.
  cfg.accessToken = token;

  long pulse = String(pulseParam->getValue()).toInt();
  cfg.pulseMs = (pulse > 0) ? static_cast<uint32_t>(pulse) : 1000;

  ConfigStore::save(cfg);
  *activeConfig = cfg;

  // No reiniciar aquí: en este punto la página de confirmación del portal
  // todavía no se envió al navegador (HTTPSend ocurre después de que este
  // callback retorna). Se difiere el reinicio a loop() con un pequeño margen.
  saveRequested = true;
  saveRequestedAtMs = millis();
  Serial.println("[NetworkManager] Configuracion guardada, reinicio programado...");

  // El intento de conexión (si se cargó una red nueva) ocurre más adelante,
  // dentro del mismo wm.process() que disparó este callback — arrancar acá
  // el parpadeo amarillo cubre esa ventana bloqueante. loop() lo apaga
  // apenas wm.process() retorna.
  LedStatus::beginConnectingBlink();
}

void setupCustomParameters(const DeviceConfig &cfg) {
  macInfoHtml = buildMacInfoHtml();
  macInfoParam = new WiFiManagerParameter(macInfoHtml.c_str());

  ipModeSelectHtml = buildIpModeSelectHtml(cfg.ipMode);
  ipModeSelectParam = new WiFiManagerParameter(ipModeSelectHtml.c_str());

  staticIpParam = new WiFiManagerParameter("staticIP", "IP Estatica", cfg.staticIP.c_str(), 15);
  subnetParam = new WiFiManagerParameter("subnet", "Mascara de subred", cfg.subnet.c_str(), 15);
  gatewayParam = new WiFiManagerParameter("gateway", "Gateway", cfg.gateway.c_str(), 15);

  closeStaticGroupParam = new WiFiManagerParameter("</div>");

  tokenParam = new WiFiManagerParameter(
      "token", "Token de Acceso (16 caracteres)", cfg.accessToken.c_str(), 16,
      "maxlength='16' minlength='16' pattern='.{16}' required");

  char pulseDefault[12];
  snprintf(pulseDefault, sizeof(pulseDefault), "%u", cfg.pulseMs);
  pulseParam = new WiFiManagerParameter(
      "pulseMs", "Tiempo de Pulso (ms)", pulseDefault, 10,
      "type='number' min='100' max='60000' step='50' required");

  wm.addParameter(macInfoParam);
  wm.addParameter(ipModeSelectParam);
  wm.addParameter(staticIpParam);
  wm.addParameter(subnetParam);
  wm.addParameter(gatewayParam);
  wm.addParameter(closeStaticGroupParam);
  wm.addParameter(tokenParam);
  wm.addParameter(pulseParam);
}

void applyStaticIpIfNeeded(const DeviceConfig &cfg) {
  if (cfg.ipMode != IP_MODE_STATIC) return;
  if (cfg.staticIP.isEmpty() || cfg.subnet.isEmpty() || cfg.gateway.isEmpty()) return;

  IPAddress ip, gateway, subnet;
  if (ip.fromString(cfg.staticIP) && gateway.fromString(cfg.gateway) && subnet.fromString(cfg.subnet)) {
    wm.setSTAStaticIPConfig(ip, gateway, subnet);
  } else {
    Serial.println("[NetworkManager] IP estatica invalida, se usara DHCP.");
  }
}

void finalizeStaConnection() {
  wm.stopConfigPortal();
  WiFi.mode(WIFI_STA);
  WiFi.softAPdisconnect(true);
  apActive = false;
  staReady = true;
  currentState = NetState::CONNECTED;
  Serial.print("[NetworkManager] Conectado. IP: ");
  Serial.println(WiFi.localIP());
}

}  // namespace

namespace NetworkManager {

void begin(DeviceConfig &config) {
  activeConfig = &config;
  currentState = NetState::CONNECTING;

  WiFi.mode(WIFI_STA);

  setupCustomParameters(config);

  wm.setCustomHeadElement(kHeadScript);
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(kConnectTimeoutSeconds);
  wm.setAPStaticIPConfig(kApIp, kApIp, kApSubnet);

  applyStaticIpIfNeeded(config);

  apNameStorage = buildApName();

  // Bloquea hasta kConnectTimeoutSeconds únicamente si hay credenciales
  // guardadas (una sola vez, en el arranque). Si falla o no hay
  // credenciales, arranca el portal cautivo no bloqueante y retorna false
  // de inmediato (setConfigPortalBlocking(false) ya fue configurado arriba).
  // El parpadeo amarillo (vía Ticker) cubre esta ventana bloqueante, ya que
  // el LED normal (update(), llamado desde loop()) no corre durante ella.
  LedStatus::beginConnectingBlink();
  bool connected = wm.autoConnect(apNameStorage.c_str());
  LedStatus::endConnectingBlink();

  if (connected) {
    finalizeStaConnection();
  } else {
    apActive = true;
    currentState = NetState::AP_CONFIG;
    Serial.print("[NetworkManager] Portal cautivo activo: ");
    Serial.println(apNameStorage);
  }
}

void loop() {
  if (apActive) {
    // saveRequested pasa a true DENTRO de wm.process() (via
    // saveParamsCallback, llamado por el manejador de /wifisave). Comparar
    // el valor de antes/después de esta llamada distingue "recién se
    // completó un intento tras guardar" (para loguear una sola vez) de
    // "ya veníamos esperando el reinicio diferido de una vuelta anterior".
    bool saveWasAlreadyPending = saveRequested;
    bool connectedNow = wm.process();
    bool saveJustHappened = saveRequested && !saveWasAlreadyPending;

    // Apenas retorna wm.process(), terminó (haya conectado o no) cualquier
    // intento de conexión que estuviera en curso dentro de esa llamada.
    LedStatus::endConnectingBlink();

    if (connectedNow) {
      finalizeStaConnection();
    } else if (saveJustHappened) {
      Serial.print("[NetworkManager] Fallo al conectar tras guardar. WiFi.status()=");
      Serial.println(WiFi.status());
    }

    // Siempre se reinicia tras un guardado (haya conectado ya en vivo o
    // no), para que cualquier cambio de configuración (p.ej. IP estática)
    // quede aplicado desde un arranque limpio.
    if (saveRequested && (millis() - saveRequestedAtMs >= kRestartGraceMs)) {
      Serial.println("[NetworkManager] Reiniciando para aplicar la configuracion...");
      Serial.flush();
      ESP.restart();
    }
    return;
  }

  if (!staReady) return;

  if (WiFi.status() == WL_CONNECTED) {
    currentState = NetState::CONNECTED;
    return;
  }

  currentState = NetState::DISCONNECTED;
  unsigned long now = millis();
  if (now - lastReconnectAttemptMs >= kReconnectIntervalMs) {
    lastReconnectAttemptMs = now;
    Serial.println("[NetworkManager] Conexion perdida, reintentando...");
    WiFi.reconnect();
  }
}

NetState state() {
  return currentState;
}

bool isReady() {
  return staReady && currentState == NetState::CONNECTED;
}

void resetWifiCredentials() {
  wm.resetSettings();
}

}  // namespace NetworkManager
