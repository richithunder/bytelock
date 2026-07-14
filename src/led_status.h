#pragma once

// Estado de conectividad de red, usado para colorear el LED integrado.
enum class NetState {
  AP_CONFIG,     // Portal cautivo de configuración activo.
  CONNECTING,    // Intentando conectar con credenciales guardadas.
  CONNECTED,     // Conectado a la red y AP apagado.
  DISCONNECTED,  // Sin red y sin AP (error/pérdida de conexión).
};

namespace LedStatus {

// Inicializa el LED integrado (RGB_BUILTIN, GPIO48).
void begin();

// Debe llamarse en cada iteración de loop(). Nunca bloquea: el parpadeo se
// controla con millis(). Prioridad: relayActive (blanco) > netState.
void update(NetState netState, bool relayActive);

// Arranca un parpadeo rápido en amarillo, manejado por un Ticker (no por
// loop()): se usa para cubrir los intentos de conexión de WiFiManager, que
// son bloqueantes (hasta ~12s) y durante los cuales loop()/update() no
// corren. Llamar justo antes de iniciar un intento de conexión.
void beginConnectingBlink();

// Detiene el parpadeo iniciado por beginConnectingBlink(). Llamar apenas
// termina el intento de conexión (haya tenido éxito o no).
void endConnectingBlink();

}  // namespace LedStatus
