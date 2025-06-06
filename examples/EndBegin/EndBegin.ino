// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// https://github.com/ESP32Async/ESPAsyncWebServer/discussions/23
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

void setup() {
  Serial.begin(115200);

#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hello, world");
  });

  server.begin();
  Serial.println("begin() - run: curl -v http://192.168.4.1/ => should succeed");
  delay(10000);

  Serial.println("end()");
  server.end();
  server.begin();
  Serial.println("begin() - run: curl -v http://192.168.4.1/ => should succeed");
}

// not needed
void loop() {
  delay(100);
}
