# Cerradura Inteligente — ESP32-S3 (N16R8)

Firmware de la lógica principal de un dispositivo de cerradura inteligente basado en ESP32-S3 (16MB Flash / 8MB PSRAM Octal), PlatformIO y el framework Arduino.

## Resumen funcional

- Configuración persistente (modo de IP, IP fija, token de acceso, tiempo de pulso del relé) almacenada en NVS vía `Preferences.h`.
- Aprovisionamiento Wi-Fi mediante un portal cautivo no bloqueante (`WiFiManager`), con parámetros personalizados y la MAC del dispositivo visible en el propio portal.
- Servidor HTTP con el endpoint `POST/GET /unlock`, protegido por un token de 16 caracteres, que activa un relé por un pulso de duración configurable.
- Indicador visual de estado mediante el LED RGB integrado.
- Factory reset físico mediante el botón BOOT de la placa.

Todo el `loop()` principal es no bloqueante: ni el servidor web, ni el pulso del relé, ni el chequeo del botón de reset usan `delay()`.

---

## 1. Pinout / Conexionado

| Función | Pin ESP32-S3 | Detalle |
|---|---|---|
| Salida al relé | **GPIO5** | Digital OUTPUT. Activo en **HIGH** por defecto (ver nota abajo). |
| Buzzer pasivo de pruebas (opcional) | **GPIO4** | Feedback sonoro en paralelo con cada pulso de apertura, vía `tone()`. No forma parte de la lógica de la cerradura; pensado para pruebas de banco. Conectar terminal + a GPIO4 y terminal − a GND. |
| Botón Factory Reset | **GPIO0** (BOOT) | Botón ya integrado en la placa DevKitC-1, `INPUT_PULLUP`. No requiere cableado adicional. |
| LED de estado | **GPIO48** (`RGB_BUILTIN`) | LED RGB direccionable ya integrado en la placa (WS2812). No requiere cableado adicional. |
| Alimentación lógica del relé | **3V3** / **GND** | La señal de control (base del transistor / entrada IN del módulo) es compatible con 3.3V. |
| Alimentación de la bobina del relé | **5V** (pin `5V`/`VIN`) / **GND** | La mayoría de los módulos de relé de 1 canal de bajo costo requieren 5V para energizar la bobina; usar el pin de 5V de la placa (no el de 3V3) y **GND común** con el ESP32. |

**Nota sobre la polaridad del relé:** el firmware usa `GPIO5` en **HIGH = relé activado** (`kRelayActiveLevel` en `src/lock_server.cpp`). Muchos módulos de relé opto-aislados de bajo costo son **activos en LOW** (el relé se energiza cuando la entrada IN está en 0V). Si al invocar `/unlock` el relé conmuta al revés de lo esperado (se abre en reposo y se cierra durante el pulso), cambiar la constante:

```cpp
// src/lock_server.cpp
constexpr uint8_t kRelayActiveLevel = LOW;  // en vez de HIGH
```

### Indicador LED de estado (GPIO48)

| Estado | Color | Patrón |
|---|---|---|
| Portal cautivo de configuración activo (esperando que el operario cargue datos) | Azul | Parpadeante (~400ms) |
| **Intentando conectar** (con credenciales guardadas al bootear, o justo después de guardar en el portal) | **Amarillo** | **Parpadeante rápido (~120ms)** |
| Conectado y listo | Verde | Fijo |
| Pulso de apertura en curso | Blanco | Fijo mientras dura el pulso |
| Sin conexión Wi-Fi y sin modo AP activo (error/desconexión) | Rojo | Parpadeante (~400ms) |

La prioridad de color es: **pulso de apertura > estado de red**. Es decir, si se recibe un `/unlock` válido mientras el LED está en verde, se ve blanco por la duración del pulso y luego vuelve a verde.

**Nota técnica sobre el parpadeo amarillo:** los intentos de conexión de `WiFiManager` son bloqueantes (hasta ~10s de por sí, más ~2s fijos de la librería tras guardar en el portal — ver la sección de Factory Reset más abajo). Durante esas ventanas el `loop()` principal no corre, así que ese parpadeo **no** se genera con el mecanismo `millis()` normal del resto de la máquina de estados: se maneja con un `Ticker` (`src/led_status.cpp`) que corre en su propia tarea y sigue parpadeando aunque el firmware esté "ocupado" tratando de conectarse.

---

## 2. Estructura del API

El servidor HTTP corre en el puerto **80** (ver sección de Buenas Prácticas sobre por qué es HTTP y no HTTPS). El único endpoint de negocio es:

```
GET  /unlock
POST /unlock
```

El token de acceso (los 16 caracteres configurados en el portal cautivo) puede enviarse de tres formas, evaluadas en este orden de precedencia:

1. **Header** `X-Access-Token` (recomendado).
2. **Query param** `token`.
3. **Body JSON**, campo `"token"`.

### Ejemplos con `curl`

**Vía header (recomendado):**
```bash
curl -i -X POST http://192.168.1.50/unlock \
  -H "X-Access-Token: 1234567890ABCDEF"
```

**Vía query param:**
```bash
curl -i "http://192.168.1.50/unlock?token=1234567890ABCDEF"
```

**Vía body JSON:**
```bash
curl -i -X POST http://192.168.1.50/unlock \
  -H "Content-Type: application/json" \
  -d '{"token":"1234567890ABCDEF"}'
```

### Respuestas

**Token correcto — `200 OK`:**
```json
{"status":"ok","message":"unlocked"}
```
El relé se activa inmediatamente y se desactiva solo, transcurrido el "Tiempo de Pulso" configurado (por defecto 1000 ms), sin bloquear el servidor. En paralelo suena el buzzer de pruebas (GPIO4, ver Pinout) como feedback audible.

**Token incorrecto, ausente o de longitud distinta a 16 — `401 Unauthorized`** (respuesta inmediata):
```json
{"status":"error","message":"unauthorized"}
```

**Ruta no encontrada — `404 Not Found`:**
```json
{"status":"error","message":"not_found"}
```

La comparación del token se hace en tiempo constante (no se corta en el primer carácter distinto) para dificultar ataques de temporización.

---

## 3. Guía de uso del Portal Cautivo

### Primer arranque / sin credenciales guardadas

1. Alimentar la placa. Primero el LED parpadea en **amarillo rápido** (intento de conexión con credenciales guardadas, si las hay). Si no hay credenciales Wi-Fi guardadas (o tras un factory reset) — o si ese intento falla — el LED pasa a parpadear en **azul** y el dispositivo levanta un punto de acceso Wi-Fi:
   - **SSID:** `byteLock` + los últimos 4 caracteres hexadecimales de la MAC del dispositivo (ej: MAC `98:A3:16:E5:A0:40` → SSID `byteLockA040`). Se arma en runtime, así que cada equipo tiene un nombre de red distinto y no hay colisiones si hay varias cerraduras cerca.
   - **Sin contraseña**
   - **IP del portal:** `192.168.4.1` (fija por código).
2. Desde un celular o notebook, conectarse a esa red Wi-Fi (`byteLock<XXXX>`).
3. La mayoría de los sistemas operativos detectan el portal cautivo automáticamente y abren solos un navegador apuntando a `http://192.168.4.1` (aviso tipo "Iniciar sesión en la red"). Si no aparece solo, navegar manualmente a esa IP. (Este comportamiento es propio de `WiFiManager`: cualquier request cuyo dominio no coincida con la IP del dispositivo se redirige automáticamente ahí; la confiabilidad del auto-popup depende del sistema operativo/navegador.)
4. Tocar **"Configure WiFi"** (o la opción equivalente del menú).
5. En esa misma página se ven, además de la lista de redes y el campo de contraseña:
   - La **dirección MAC** del dispositivo, mostrada como texto informativo en la parte superior del formulario (útil para identificar el equipo, por ejemplo al darlo de alta en el router o en un sistema de inventario).
   - **Modo de IP**: selector desplegable `DHCP` / `IP Fija`.
     - Si se elige `DHCP`, los campos de abajo se ocultan y no son necesarios.
     - Si se elige `IP Fija`, aparecen los campos **IP Estática**, **Máscara de subred** y **Gateway** — completarlos con los datos de la red destino (ej: `192.168.1.50`, `255.255.255.0`, `192.168.1.1`).
   - **Token de Acceso**: campo de texto que exige **exactamente 16 caracteres** (el formulario no deja enviar un valor de otra longitud).
   - **Tiempo de Pulso (ms)**: campo numérico con la duración en milisegundos que el relé permanecerá activado en cada apertura (por defecto 1000).
6. Seleccionar la red Wi-Fi de destino, cargar su contraseña, completar los campos anteriores y tocar **Guardar/Save**. El LED pasa a **amarillo parpadeante rápido** mientras se guarda (~2s) y se intenta la conexión (hasta ~8s) — es normal que el portal tarde unos segundos en responder durante esta ventana.
7. El dispositivo persiste toda la configuración en `Preferences` (NVS) y **se reinicia automáticamente** para aplicar los cambios, haya podido conectarse en el momento o no (esto garantiza que una IP estática recién cargada quede aplicada desde un arranque limpio).
8. Al volver a arrancar, intenta conectarse a la red configurada (aplicando la IP fija si corresponde). El LED pasa a **verde fijo** cuando la conexión es exitosa y el punto de acceso se apaga por completo. Si falla (contraseña incorrecta, red de 5GHz — el ESP32 solo soporta 2.4GHz —, señal débil), vuelve a levantar el portal (`byteLock<XXXX>`) para reintentar; el motivo del fallo queda registrado por serial (`WiFi.status()`).

### Reconfigurar más adelante

Para volver a entrar al portal (cambiar de red, IP, token o tiempo de pulso), hacer un **factory reset físico** (ver sección siguiente): al no quedar credenciales guardadas, el dispositivo vuelve a levantar el portal (`byteLock<XXXX>`) en el siguiente arranque.

### Factory reset físico

1. Mantener presionado el botón **BOOT** de la placa (GPIO0) de forma continua durante **5 segundos o más**.
2. El firmware borra las credenciales Wi-Fi (`WiFiManager::resetSettings()`) y toda la configuración guardada en `Preferences` (modo de IP, IP fija, token, tiempo de pulso).
3. El dispositivo se reinicia solo y vuelve a levantar el portal cautivo.

**Nota técnica:** el mantenido se detecta por **interrupción de hardware** (`attachInterrupt` en GPIO0), no por sondeo (*polling*) del `loop()`. Esto es intencional: durante un intento de conexión (LED amarillo parpadeante) el `loop()` principal queda bloqueado hasta ~10s, y un sondeo normal no mediría bien un mantenido que empiece en esa ventana. Gracias a la interrupción, **mantener BOOT funciona de forma confiable incluso mientras la placa está intentando conectarse**.

---

## 4. Buenas Prácticas

### Por qué HTTP y no HTTPS, y cómo securizar en producción

Este firmware expone `/unlock` por **HTTP simple** (`WebServer`, puerto 80), no HTTPS. Es una decisión deliberada: levantar un servidor TLS real en el ESP32-S3 implica generar/embeber un certificado (autofirmado o propio), un handshake de 1-3 segundos por conexión, y varios KB adicionales de RAM/flash — overhead considerable para un endpoint de bajísimo volumen como el de una cerradura. Para producción, se recomienda securizar el **canal**, no el propio firmware, con una de estas opciones (de más a menos recomendada):

1. **Red aislada / VLAN dedicada** para dispositivos de cerradura, sin salida a Internet y con acceso restringido solo desde la app/controlador autorizado.
2. **VPN** (WireGuard/OpenVPN) entre el controlador que invoca `/unlock` y la red local del dispositivo, de forma que el tráfico HTTP viaje cifrado por el túnel.
3. **Proxy TLS delante del dispositivo** (por ejemplo nginx o Caddy corriendo en un gateway/Raspberry Pi de la misma red), que termine HTTPS y reenvíe por HTTP simple solo dentro de la red confiable.
4. **Migración futura a `esp32_https_server`** (u otra librería de TLS embebido) si el proyecto llega a requerir HTTPS de punta a punta en el propio microcontrolador, aceptando el costo de RAM/flash/latencia que eso implica.

Independientemente del transporte, **nunca** exponer el puerto 80 del dispositivo directamente a Internet sin alguna de las capas anteriores.

### Manejo de memoria y PSRAM

Esta placa tiene 8MB de PSRAM octal (`-DBOARD_HAS_PSRAM`, `qio_opi`), pero **ninguna de las librerías usadas aquí la utiliza por defecto**: `WiFiManager`, `WebServer` y `ArduinoJson` (v7, con `JsonDocument`) reservan sus buffers en el heap interno (~320KB de RAM) salvo que se les indique explícitamente lo contrario. Para los payloads actuales (JSON de una línea, formularios cortos) esto es más que suficiente y más rápido que pasar por PSRAM.

Si el proyecto crece (por ejemplo, para guardar un log de accesos, múltiples usuarios/tokens, o servir archivos más grandes), considerar:
- Reservar buffers grandes explícitamente en PSRAM con `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` o `ps_malloc(size)`.
- Evitar concatenar `String` de forma repetida dentro de bucles para armar HTML/JSON grandes: fragmenta el heap interno rápidamente. Preferir buffers reservados de antemano (`reserve()`) o serialización directa a un `Stream`.

### Hardening del token de acceso

- El token se guarda en NVS **sin cifrar** (comportamiento por defecto de `Preferences`/NVS). Para un despliegue real, evaluar habilitar **NVS Encryption** junto con **Flash Encryption** de la ESP-IDF, de forma que el token no pueda leerse extrayendo la flash del dispositivo.
- Esta versión **no implementa rate-limiting** ni bloqueo tras intentos fallidos repetidos contra `/unlock`: cualquier request con token incorrecto recibe `401` inmediatamente, sin penalización. Si el endpoint queda expuesto a una red no totalmente confiable, se recomienda agregar un límite de intentos por IP/tiempo (por ejemplo, bloquear temporalmente tras N fallos en M segundos) como mejora futura.
- Rotar el token periódicamente reingresando al portal cautivo (vía factory reset) es la única forma de cambiarlo en esta versión; no hay un endpoint remoto para actualizarlo (a propósito, para no exponer esa superficie de ataque por HTTP simple).

---

## Comandos de PlatformIO

- Build: `pio run`
- Upload: `pio run -t upload`
- Monitor serie (115200 baud, USB nativo): `pio device monitor`
- Build + upload + monitor: `pio run -t upload -t monitor`
