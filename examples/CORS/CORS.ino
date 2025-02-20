// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// How to use CORS middleware
//

#include <Arduino.h>
#ifdef ESP32
#include <AsyncTCP.h>
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#include <WiFi.h>
#endif

#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);
static AsyncCorsMiddleware cors;

void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  cors.setOrigin("http://192.168.4.1");
  cors.setMethods("POST, GET, OPTIONS, DELETE");
  cors.setHeaders("X-Custom-Header");
  cors.setAllowCredentials(false);
  cors.setMaxAge(600);

  server.addMiddleware(&cors);

  // Test CORS preflight request
  // curl -v -X OPTIONS -H "origin: http://192.168.4.1" http://192.168.4.1/cors
  //
  // Test CORS request
  // curl -v -H "origin: http://192.168.4.1" http://192.168.4.1/cors
  //
  // Test non-CORS request
  // curl -v http://192.168.4.1/cors
  //
  server.on("/cors", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hello, world!");
  });

  server.begin();
}

// not needed
void loop() {
  delay(100);
}
