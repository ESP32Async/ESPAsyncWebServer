// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

//
// Shows how one handler object can serve multiple URIs and accept
// multiple HTTP methods.
//
// A handler holds a LIST of URI matchers instead of a single one:
//   - setUri()  replaces the list with one matcher (backward-compatible)
//   - addUri()  appends an additional URI matcher
//   - clearUris() removes all matchers
//
// setMethod() accepts a composite mask: handler.setMethod(HTTP_GET | HTTP_POST)
// so one handler can serve multiple URIs × multiple methods.
// Use separate handlers when different methods need different callbacks
// (e.g. a GET handler and a POST handler with onBody/onUpload callbacks).
//
// Non-owning registration (addHandler by reference) lets the handler
// live in static RAM or PSRAM without any heap allocation.
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
// One handler, many URIs — lives in static RAM (no heap allocation).
// The same object serves /hello, /hi, and /greet/*.
// ---------------------------------------------------------------------------
static AsyncCallbackWebHandler greetHandler;

// ---------------------------------------------------------------------------
// Another handler in PSRAM (ESP32 only), also serving multiple URIs.
// ---------------------------------------------------------------------------
#if defined(ESP32)
static AsyncCallbackWebHandler *apiHandler = nullptr;
static void *apiHandlerMem = nullptr;
#endif

// ---------------------------------------------------------------------------
// A classic server-owned handler (convenience API, fully backward compatible).
// ---------------------------------------------------------------------------
// (see server.on() below)

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

#if ASYNCWEBSERVER_WIFI_SUPPORTED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-multi-uri");
#endif

  // --- 1. One handler, three URIs, two methods (static RAM) ------------------
  // The handler only defines WHAT to do — its URI list defines WHERE.
  greetHandler.onRequest([](AsyncWebServerRequest *request) {
    String msg = "Hello from " + request->url();
    request->send(200, "text/plain", msg);
  });
  // setMethod accepts a composite: this handler serves GET and POST
  greetHandler.setMethod(HTTP_GET | HTTP_POST);

  // Same handler object serving multiple URIs:
  greetHandler.setUri(AsyncURIMatcher::exact("/hello"));
  greetHandler.addUri(AsyncURIMatcher::exact("/hi"));
  greetHandler.addUri(AsyncURIMatcher::prefix("/greet/"));

  // Non-owning registration (by reference) — greetHandler lives in static RAM (.bss)
  server.addHandler(greetHandler);

  // --- 2. PSRAM handler serving multiple URIs (ESP32 only) ------------------
#if defined(ESP32)
  if (psramFound()) {
    apiHandlerMem = heap_caps_malloc(sizeof(AsyncCallbackWebHandler), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (apiHandlerMem) {
      apiHandler = new (apiHandlerMem) AsyncCallbackWebHandler();
      apiHandler->onRequest([](AsyncWebServerRequest *request) {
        String json = "{\"path\":\"" + request->url() + "\",\"region\":\"psram\"}";
        request->send(200, "application/json", json);
      });
      apiHandler->setMethod(HTTP_GET);
      // Same PSRAM handler serving two URIs — non-owning (borrow by reference):
      apiHandler->setUri(AsyncURIMatcher::prefix("/api/"));
      apiHandler->addUri(AsyncURIMatcher::exact("/status"));
      server.addHandler(*apiHandler);
      Serial.printf("API handler allocated in PSRAM at %p\n", apiHandlerMem);
    }
  }
#endif

  // --- 3. When methods need different callbacks, use different handlers -----
  // greetHandler accepts GET|POST but has the same onRequest for both.
  // For a POST with onBody/onUpload, create a separate handler:
  static AsyncCallbackWebHandler submitHandler;
  submitHandler.setMethod(HTTP_POST);
  submitHandler.setUri(AsyncURIMatcher::exact("/submit"));
  submitHandler.onRequest([](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Submitted");
  });
  submitHandler.onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // receive the request body here
  });
  server.addHandler(submitHandler);  // non-owning, static RAM

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
  Serial.printf("Connect to AP 'esp-multi-uri' and open http://%s\n",
                WiFi.softAPIP().toString().c_str());
#endif
  Serial.println("Try (greetHandler serves multiple URIs with GET|POST):");
  Serial.println("  http://192.168.4.1/hello       → greetHandler");
  Serial.println("  http://192.168.4.1/hi          → greetHandler (same object!)");
  Serial.println("  http://192.168.4.1/greet/world → greetHandler (prefix URI)");
#if defined(ESP32)
  Serial.println("Try (PSRAM handler, ESP32 only):");
  Serial.println("  http://192.168.4.1/api/test    → PSRAM apiHandler");
  Serial.println("  http://192.168.4.1/status      → PSRAM apiHandler (same object!)");
#endif
  Serial.println("Try (separate handler for POST with onBody):");
  Serial.println("  POST http://192.168.4.1/submit  → submitHandler");
  Serial.println("Try (server-owned convenience handler):");
  Serial.println("  http://192.168.4.1/ping        → server.on() handler");
  Serial.println("Try (no handler matches → catch-all):");
  Serial.println("  http://192.168.4.1/unknown     → 404 catch-all");
}

void loop() {
  // Nothing to do — the async server handles everything.
}
