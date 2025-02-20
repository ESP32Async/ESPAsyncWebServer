// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// Shows how to send and receive Message Pack data
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

#if __has_include("ArduinoJson.h")
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncMessagePack.h>
#endif

static AsyncWebServer server(80);

#if __has_include("ArduinoJson.h")
static AsyncCallbackMessagePackWebHandler *handler = new AsyncCallbackMessagePackWebHandler("/msgpack2");
#endif

void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

#if __has_include("ArduinoJson.h")
  //
  // sends MessagePack using AsyncMessagePackResponse
  //
  // curl -v http://192.168.4.1/msgpack1
  //
  server.on("/msgpack1", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncMessagePackResponse *response = new AsyncMessagePackResponse();
    JsonObject root = response->getRoot().to<JsonObject>();
    root["hello"] = "world";
    response->setLength();
    request->send(response);
  });

  // Send MessagePack using AsyncResponseStream
  //
  // curl -v http://192.168.4.1/msgpack2
  //
  server.on("/msgpack2", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/msgpack");
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["foo"] = "bar";
    serializeMsgPack(root, *response);
    request->send(response);
  });

  handler->setMethod(HTTP_POST | HTTP_PUT);
  handler->onRequest([](AsyncWebServerRequest *request, JsonVariant &json) {
    serializeJson(json, Serial);
    AsyncMessagePackResponse *response = new AsyncMessagePackResponse();
    JsonObject root = response->getRoot().to<JsonObject>();
    root["hello"] = json.as<JsonObject>()["name"];
    response->setLength();
    request->send(response);
  });

  server.addHandler(handler);
#endif

  server.begin();
}

// not needed
void loop() {
  delay(100);
}
