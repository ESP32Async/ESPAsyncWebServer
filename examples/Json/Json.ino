// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// Shows how to send and receive Json data
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
static AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/json2");
#endif

void setup() {
  Serial.begin(115200);

#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

#if __has_include("ArduinoJson.h")
  //
  // sends JSON using AsyncJsonResponse
  //
  // curl -v http://192.168.4.1/json1
  //
  server.on("/json1", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().to<JsonObject>();
    root["hello"] = "world";
    response->setLength();
    request->send(response);
  });

  // Send JSON using AsyncResponseStream
  //
  // curl -v http://192.168.4.1/json2
  //
  server.on("/json2", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["foo"] = "bar";
    serializeJson(root, *response);
    Serial.println();
    request->send(response);
  });

  // curl -v -X POST -H 'Content-Type: application/json' -d '{"name":"You"}' http://192.168.4.1/json2
  // curl -v -X PUT -H 'Content-Type: application/json' -d '{"name":"You"}' http://192.168.4.1/json2
  //
  // edge cases:
  //
  // curl -v -X POST -H "Content-Type: application/json" -d "1234" -H "Content-Length: 5" http://192.168.4.1/json2 => rx timeout
  // curl -v -X POST -H "Content-Type: application/json" -d "1234" -H "Content-Length: 2" http://192.168.4.1/json2 => 12
  // curl -v -X POST -H "Content-Type: application/json" -d "1234" -H "Content-Length: 4" http://192.168.4.1/json2 => 1234
  // curl -v -X POST -H "Content-Type: application/json" -d "1234" -H "Content-Length: 10" http://192.168.4.1/json2 => rx timeout
  // curl -v -X POST -H "Content-Type: application/json" -d "12345678" -H "Content-Length: 8" http://192.168.4.1/json2 => 12345678
  // curl -v -X POST -H "Content-Type: application/json" -d "123456789" -H "Content-Length: 8" http://192.168.4.1/json2 => 12345678
  // curl -v -X POST -H "Content-Type: application/json" -d "123456789" -H "Content-Length: 9" http://192.168.4.1/json2 => 413: Content length exceeds maximum allowed
  handler->setMaxContentLength(8);
  handler->setMethod(HTTP_POST | HTTP_PUT);
  handler->onRequest([](AsyncWebServerRequest *request, JsonVariant &json) {
    serializeJson(json, Serial);
    Serial.println();
    AsyncJsonResponse *response = new AsyncJsonResponse();
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
