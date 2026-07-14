# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

PlatformIO/Arduino firmware for a **Smart Lock** device targeting an ESP32-S3 N16R8 module (16MB flash, 8MB octal-SPI PSRAM). The device provisions itself over a WiFiManager captive portal, persists its configuration in NVS (`Preferences`), and exposes a token-protected HTTP endpoint (`/unlock`) that pulses a relay. See `README.md` for the pinout, API examples, captive-portal walkthrough, and production hardening notes — read it before making networking/security-relevant changes.

## Commands

This project uses PlatformIO (env name: `esp32-s3-devkitc-1`, defined in `platformio.ini`). Use the `pio` CLI, or the PlatformIO IDE extension in VS Code (already configured via `.vscode/`).

- Build: `pio run`
- Upload to board: `pio run -t upload`
- Open serial monitor (115200 baud, native USB CDC): `pio device monitor`
- Build and immediately monitor: `pio run -t upload -t monitor`
- Clean build artifacts: `pio run -t clean`
- Run unit tests (PlatformIO Test Runner, once tests exist under `test/`): `pio test`
- Fetch/update library dependencies after editing `lib_deps`: `pio pkg install`

## Architecture

- **`platformio.ini`** — single environment `esp32-s3-devkitc-1`, `framework = arduino`. Key hardware-specific settings:
  - Flash: `board_upload.flash_size = 16MB`, partitions from `default_16MB.csv`.
  - PSRAM: `board_build.arduino.memory_type = qio_opi` plus `-DBOARD_HAS_PSRAM` and `-mfix-esp32-psram-cache-issue` build flags — required for the octal PSRAM on the N16R8 to be usable and cache-coherent.
  - USB: native USB CDC is enabled on boot (`-DARDUINO_USB_CDC_ON_BOOT=1`), with MSC and DFU modes disabled. Serial output (`Serial`) goes over the native USB-OTG port, not a UART-to-USB bridge chip.
  - `lib_deps`: `tzapu/WiFiManager` (captive portal + provisioning) and `bblanchon/ArduinoJson` v7 (`/unlock` request/response bodies).
- **`src/main.cpp`** — orchestrator only. `setup()` loads config and starts `NetworkManager`; `loop()` is 100% non-blocking: it drives `NetworkManager::loop()`, `LedStatus::update()`, a factory-reset button check (GPIO0/BOOT, 5s hold), and — once `NetworkManager::isReady()` — `LockServer`'s request handling and relay-pulse timer. The button check itself is `attachInterrupt`-driven (`CHANGE` on GPIO0, `IRAM_ATTR` ISR records the press timestamp in a `volatile`), not plain polling — see the important gotcha below.
- **`src/config_store.h/.cpp`** — thin `Preferences` (NVS, namespace `lockcfg`) wrapper around a `DeviceConfig` struct (IP mode, static IP/subnet/gateway, 16-char access token, relay pulse duration in ms). This is the single source of truth for persisted settings; `NetworkManager` and `LockServer` both take a `DeviceConfig` rather than touching `Preferences` directly.
- **`src/network_manager.h/.cpp`** — wraps `WiFiManager` in non-blocking mode (`setConfigPortalBlocking(false)`, driven via `wm.process()` from `loop()`). AP SSID is built at runtime (`buildApName()`): `"byteLock" + last 4 hex chars of WiFi.macAddress()`, stored in a file-scope `String` since `wm.autoConnect()` keeps the raw pointer. AP IP is pinned explicitly via `wm.setAPStaticIPConfig(192.168.4.1, ...)`. Builds the captive-portal's custom parameters (MAC display, DHCP/Static-IP selector with injected JS show/hide, static IP fields, token, pulse) as raw-HTML `WiFiManagerParameter`s — see the comments in this file before changing the portal form, since WiFiManager's parameter/save-callback semantics (`getID()==NULL` params vs. tracked ones, `doParamSave()` timing relative to the HTTP response) are non-obvious and were reverse-engineered from the vendored library source in `.pio/libdeps/.../WiFiManager/WiFiManager.cpp`. Applies static IP via `wm.setSTAStaticIPConfig()` before connecting. After any portal save, the device **always restarts** (`saveRequested` + `kRestartGraceMs`, checked in `loop()`) regardless of whether the immediate connect attempt in that same request succeeded — this is required so a freshly-saved static IP actually gets applied (it's only read once, at `begin()`, from the *pre-save* config). On successful connect: `wm.stopConfigPortal()` + `WiFi.mode(WIFI_STA)` + `WiFi.softAPdisconnect(true)`. Exposes a `NetState` (`AP_CONFIG`/`CONNECTING`/`CONNECTED`/`DISCONNECTED`) consumed by `LedStatus`.
- **`src/lock_server.h/.cpp`** — `WebServer` (port 80, plain HTTP — see README for why) exposing `GET/POST /unlock`. Token accepted via `X-Access-Token` header, `token` query param, or JSON body, in that precedence order; compared in constant time. On match, triggers the relay (GPIO5, active-HIGH by default) and arms a `millis()`-based off-timer (`update()`, polled from `loop()`) instead of `delay()`. Also fires a non-blocking `tone()` beep on a bench-testing buzzer (GPIO4, optional, not part of the lock logic).
- **`src/led_status.h/.cpp`** — drives the onboard RGB LED (`RGB_BUILTIN`, GPIO48) as a non-blocking status indicator: blinking blue while the captive portal is idle/waiting, solid green when ready, blinking red when disconnected with no AP fallback, solid white during a relay pulse (highest priority). The "actively connecting" state (fast yellow blink) is *not* driven by `update()`/`millis()` — see gotcha below — it's `beginConnectingBlink()`/`endConnectingBlink()`, backed by a `Ticker` (`Ticker.h`, core-provided) running in its own FreeRTOS task.

### Important gotcha: WiFiManager's connect attempts are synchronously blocking

Even with `setConfigPortalBlocking(false)`, both (a) `wm.autoConnect()`'s attempt with saved credentials at boot, and (b) the connect attempt WiFiManager triggers internally right after a portal save (inside the very same `wm.process()` call that handled the `/wifisave` POST — see `processConfigPortal()`'s `if(connect){...}` block in the vendored source), are fully synchronous and can block for up to `_cpclosedelay` (2000ms, hardcoded in the library) + `setConnectTimeout()` (currently 8s) — i.e. `NetworkManager::loop()` simply doesn't return for up to ~10s during those windows. Anything driven by polling from `loop()` (the old factory-reset button check, the old LED blink) is invisible/unreliable during that time. The fix used throughout this codebase is to move time-sensitive feedback off the main loop entirely: the reset button uses a GPIO interrupt, and the "connecting" LED state uses a `Ticker`. Keep this in mind before adding new polling-based logic that needs to stay responsive during a connection attempt.
- **`include/`, `lib/`, `test/`** — still empty PlatformIO-managed placeholders; not used by this project.
- Not currently a git repository.
