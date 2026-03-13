// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

//
// HTTP Method usage example and check compatibility with Arduino HTTP Methods
//

#include <Arduino.h>

#if !defined(ESP8266)
// simulate asyncws project being used with another library using Arduino HTTP Methods
#include <HTTP_Method.h>
#endif

#if defined(ESP32) || defined(LIBRETINY)
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

#if ASYNCWEBSERVER_WIFI_SUPPORTED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  // curl -v http://192.168.4.1/get-or-post => Hello
  // curl -v -X POST -d "a=b" http://192.168.4.1/get-or-post => Hello
  // curl -v -X PUT -d "a=b" http://192.168.4.1/get-or-post => 404
  // curl -v -X PATCH -d "a=b" http://192.168.4.1/get-or-post => 404
  server.on("/get-or-post", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hello");
  });

  // curl -v http://192.168.4.1/any => Hello
  // curl -v -X POST -d "a=b" http://192.168.4.1/any => Hello
  // curl -v -X PUT -d "a=b" http://192.168.4.1/any => Hello
  // curl -v -X PATCH -d "a=b" http://192.168.4.1/any => Hello
  server.on("/any", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hello");
  });

  server.begin();
}

// not needed
void loop() {
  delay(100);
}
