//
// Shows how to create routers
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
static AsyncRouter settingsRouter(&server, "/settings");
static AsyncRouter exampleRouter(&server, "/example");

void timeSettingController(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Hello from /settings/time");
}

void otherSettingController(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Hello from /settings/other");
}

void exampleController(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Hello from /example/test");
}

void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  // Create the routes and assign controller functions to them
  settingsRouter.on("/time", timeSettingController);
  settingsRouter.on("/other", otherSettingController);

  // An other example
  exampleRouter.on("/test", exampleController);

  // Inline controller function
  exampleRouter.on("/test2", [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Hello from /example/test2");
  });

  // Add the routers to the server
  server.addRouter(&settingsRouter);
  server.addRouter(&exampleRouter);

  server.begin();
}

// not needed
void loop() {
  delay(100);
}
