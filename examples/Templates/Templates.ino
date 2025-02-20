// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// Shows how to serve a static and dynamic template
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
#include <LittleFS.h>

static AsyncWebServer server(80);

static const char *htmlContent PROGMEM = R"(
<!DOCTYPE html>
<html>
<body>
    <h1>Hello, %USER%</h1>
</body>
</html>
)";

static const size_t htmlContentLength = strlen_P(htmlContent);

void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

#ifdef ESP32
  LittleFS.begin(true);
#else
  LittleFS.begin();
#endif

  {
    File f = LittleFS.open("/template.html", "w");
    assert(f);
    f.print(htmlContent);
    f.close();
  }

  // Serve the static template file
  //
  // curl -v http://192.168.4.1/template.html
  server.serveStatic("/template.html", LittleFS, "/template.html");

  // Serve the static template with a template processor
  //
  // ServeStatic static is used to serve static output which never changes over time.
  // This special endpoints automatically adds caching headers.
  // If a template processor is used, it must ensure that the outputted content will always be the same over time and never changes.
  // Otherwise, do not use serveStatic.
  // Example below: IP never changes.
  //
  // curl -v http://192.168.4.1/index.html
  server.serveStatic("/index.html", LittleFS, "/template.html").setTemplateProcessor([](const String &var) -> String {
    if (var == "USER") {
      return "Bob";
    }
    return emptyString;
  });

  // Serve a template with dynamic content
  //
  // to serve a template with dynamic content (output changes over time), use normal
  // Example below: content changes over tinme do not use serveStatic.
  //
  // curl -v http://192.168.4.1/dynamic.html
  server.on("/dynamic.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/template.html", "text/html", false, [](const String &var) -> String {
      if (var == "USER") {
        return String("Bob ") + millis();
      }
      return emptyString;
    });
  });

  server.begin();
}

// not needed
void loop() {
  delay(100);
}
