// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

//
// Shows how to control where handler objects are allocated.
//
// The server offers two registration styles:
//   - addHandler(ptr) — server owns the handler (backward compatible)
//   - addHandler(ref) — non-owning borrow, the user manages the handler's lifetime
//
// With non-owning registration you can place handlers in:
//   - Static / global RAM  (no heap allocation at all)
//   - PSRAM                (ESP32: placement new with heap_caps_malloc)
//
// The same applies to the catch-all handler — via the constructor or setCatchAllHandler().
//

#include <Arduino.h>
#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#include <WiFi.h>
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#include <WiFi.h>
#endif

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// ---------------------------------------------------------------------------
// Custom catch-all handler — static RAM, non-owning.
// Passed to the server constructor so it's used from the very first request.
// ---------------------------------------------------------------------------
static AsyncCallbackWebHandler catchAllHandler;

static AsyncWebServer server(80, catchAllHandler);  // non-owning reference

// ---------------------------------------------------------------------------
// 1. Static RAM handler — allocated in .bss, zero heap usage
// ---------------------------------------------------------------------------
static AsyncCallbackWebHandler statusHandler;

// ---------------------------------------------------------------------------
// 2. PSRAM handler (ESP32 only) — allocated via placement new in PSRAM
// ---------------------------------------------------------------------------
#if defined(ESP32)
static AsyncCallbackWebHandler *apiHandler = nullptr;
static void *apiHandlerMem = nullptr;
#endif

// ---------------------------------------------------------------------------
// 3. Server-owned handler — classic style, server manages lifetime
// ---------------------------------------------------------------------------
// (see server.on() below)

void setup() {
  Serial.begin(115200);
  while (!Serial);

#if ASYNCWEBSERVER_WIFI_SUPPORTED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-custom-alloc");
#endif

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
  }

  // --- 1. Static RAM handler ------------------------------------------------
  // No heap allocation — the object lives in .bss for the entire program.
  statusHandler.setUri(AsyncURIMatcher::exact("/status"));
  statusHandler.setMethod(HTTP_GET);
  statusHandler.onRequest([](AsyncWebServerRequest *request) {
#if defined(ESP32)
    String json = "{\"uptime\":" + String(millis()) + ",\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
#else
    String json = "{\"uptime\":" + String(millis()) + "}";
#endif
    request->send(200, "application/json", json);
  });
  server.addHandler(statusHandler);  // non-owning reference overload

  // --- 2. PSRAM handler (ESP32 only) ----------------------------------------
#if defined(ESP32)
  if (psramFound()) {
    apiHandlerMem = heap_caps_malloc(sizeof(AsyncCallbackWebHandler), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (apiHandlerMem) {
      apiHandler = new (apiHandlerMem) AsyncCallbackWebHandler();
      apiHandler->setUri(AsyncURIMatcher::prefix("/api"));
      apiHandler->setMethod(HTTP_GET);
      apiHandler->onRequest([](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true,\"region\":\"psram\"}");
      });
      // Non-owning borrow: the server stores a reference; we manage PSRAM lifetime.
      server.addHandler(*apiHandler);
      Serial.printf("API handler allocated in PSRAM at %p (%u bytes free in PSRAM)\n", apiHandlerMem, ESP.getFreePsram());
    } else {
      Serial.println("PSRAM allocation failed for API handler");
    }
  } else {
    Serial.println("No PSRAM found — skipping PSRAM handler demo");
  }
#endif

  // --- 3. Server-owned handler (classic convenience API) --------------------
  server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hello from a server-owned handler!");
  });

  // --- Static file serving (server-owned) -----------------------------------
  server.serveStatic("/static", LittleFS, "/www");

  // --- Configure the custom catch-all (passed to constructor above) ----------
  catchAllHandler.onRequest([](AsyncWebServerRequest *request) {
    String msg = "404 - Not found: " + request->url();
    request->send(404, "text/plain", msg);
  });

  server.begin();
  Serial.println("HTTP server started");
#if ASYNCWEBSERVER_WIFI_SUPPORTED
  Serial.printf("Connect to AP 'esp-custom-alloc' and open http://%s\n", WiFi.softAPIP().toString().c_str());
#endif
  Serial.println("Try:");
  Serial.println("  http://192.168.4.1/status   (static RAM handler)");
#if defined(ESP32)
  Serial.println("  http://192.168.4.1/api/test (PSRAM handler, ESP32 only)");
#endif
  Serial.println("  http://192.168.4.1/hello    (server-owned handler)");
  Serial.println("  http://192.168.4.1/anything (custom catch-all)");
  Serial.println("  http://192.168.4.1/static/  (static file serving)");
}

void loop() {
  // Nothing to do — the async server handles everything.
}
