// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

//
// Shows how one handler object can serve multiple routes (Phase 2: route table).
//
// The addRoute() API decouples URI matching from the handler object:
//   - The server's routing table stores { URI matcher, HTTP method, handler ptr }
//   - One handler can be attached to many routes
//   - URI/method matching is done by the server, not inside the handler
//
// Routes are evaluated before legacy handlers (registered via addHandler).
//
// The same handler can also be placed in PSRAM or static RAM (Phase 1).
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

static AsyncWebServer server(80);

// ---------------------------------------------------------------------------
// One handler, many routes — lives in static RAM (no heap allocation).
// The same object serves /hello, /hi, and /greet/* (Phase 1 + Phase 2).
// ---------------------------------------------------------------------------
static AsyncCallbackWebHandler greetHandler;

// ---------------------------------------------------------------------------
// Another handler in PSRAM (ESP32 only), also serving multiple routes.
// ---------------------------------------------------------------------------
#if defined(ESP32)
static AsyncCallbackWebHandler *apiHandler = nullptr;
static void *apiHandlerMem = nullptr;
#endif

// ---------------------------------------------------------------------------
// A classic server-owned handler (convenience API, Phase 1 backward compat).
// ---------------------------------------------------------------------------
// (see server.on() below)

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

#if ASYNCWEBSERVER_WIFI_SUPPORTED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-multi-route");
#endif

  // --- 1. One handler, three routes (static RAM) ----------------------------
  // The handler only defines WHAT to do — the server's route table defines WHERE.
  greetHandler.onRequest([](AsyncWebServerRequest *request) {
    String msg = "Hello from " + request->url();
    request->send(200, "text/plain", msg);
  });

  // Same handler object on multiple routes:
  server.addRoute(AsyncURIMatcher::exact("/hello"), HTTP_GET, greetHandler);
  server.addRoute(AsyncURIMatcher::exact("/hi"), HTTP_GET, greetHandler);
  server.addRoute(AsyncURIMatcher::prefix("/greet/"), HTTP_GET, greetHandler);
  // greetHandler is registered non-owning — it lives in static RAM (.bss).

  // --- 2. PSRAM handler on multiple routes (ESP32 only) ---------------------
#if defined(ESP32)
  if (psramFound()) {
    apiHandlerMem = heap_caps_malloc(sizeof(AsyncCallbackWebHandler), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (apiHandlerMem) {
      apiHandler = new (apiHandlerMem) AsyncCallbackWebHandler();
      apiHandler->onRequest([](AsyncWebServerRequest *request) {
        String json = "{\"path\":\"" + request->url() + "\",\"region\":\"psram\"}";
        request->send(200, "application/json", json);
      });
      // Same PSRAM handler on two routes — non-owning via pointer:
      server.addRoute(AsyncURIMatcher::prefix("/api/"), HTTP_GET, apiHandler, false);
      server.addRoute(AsyncURIMatcher::exact("/status"), HTTP_GET, apiHandler, false);
      Serial.printf("API handler allocated in PSRAM at %p\n", apiHandlerMem);
    }
  }
#endif

  // --- 3. Different method on same handler ----------------------------------
  // The route table also filters by HTTP method — POST /hello is not matched
  // by the GET routes above, it would fall through to the catch-all.
  greetHandler.onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // accept body but respond in handleRequest
  });

  // --- 4. Server-owned handler (classic convenience API) --------------------
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "pong");
  });

  // --- 5. Custom catch-all --------------------------------------------------
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found: " + request->url());
  });

  server.begin();
  Serial.println("HTTP server started");
#if ASYNCWEBSERVER_WIFI_SUPPORTED
  Serial.printf("Connect to AP 'esp-multi-route' and open http://%s\n",
                WiFi.softAPIP().toString().c_str());
#endif
  Serial.println("Try (all served by the SAME greetHandler object):");
  Serial.println("  http://192.168.4.1/hello       → greetHandler");
  Serial.println("  http://192.168.4.1/hi          → greetHandler (same object!)");
  Serial.println("  http://192.168.4.1/greet/world → greetHandler (prefix route)");
#if defined(ESP32)
  Serial.println("Try (PSRAM handler, ESP32 only):");
  Serial.println("  http://192.168.4.1/api/test    → PSRAM apiHandler");
  Serial.println("  http://192.168.4.1/status      → PSRAM apiHandler (same object!)");
#endif
  Serial.println("Try (server-owned convenience handler):");
  Serial.println("  http://192.168.4.1/ping        → server.on() handler");
  Serial.println("Try (no route matches → catch-all):");
  Serial.println("  http://192.168.4.1/unknown     → 404 catch-all");
  Serial.println();
  Serial.println("Note: POST /hello won't match (routes are GET-only) → 404 catch-all");
}

void loop() {
  // Nothing to do — the async server handles everything.
}
