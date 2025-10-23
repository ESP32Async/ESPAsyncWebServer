// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// Example to send a large response and control the filling of the buffer.
//
// This is also a MRE for:
// - https://github.com/ESP32Async/ESPAsyncWebServer/issues/242
// - https://github.com/ESP32Async/ESPAsyncWebServer/issues/315
//

#include <Arduino.h>
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

static const size_t totalResponseSize = 16 * 1000;  // 16 KB
static char fillChar = 'A';

class CustomResponse : public AsyncAbstractResponse {
public:
  explicit CustomResponse() {
    _code = 200;
    _contentType = "text/plain";
    _sendContentLength = false;
  }

  bool _sourceValid() const override {
    return true;
  }

  size_t _fillBuffer(uint8_t *buf, size_t buflen) override {
    if (_sent == RESPONSE_TRY_AGAIN) {
      Serial.println("Simulating temporary unavailability of data...");
      _sent = 0;
      return RESPONSE_TRY_AGAIN;
    }
    size_t remaining = totalResponseSize - _sent;
    if (remaining == 0) {
      return 0;
    }
    if (buflen > remaining) {
      buflen = remaining;
    }
    Serial.printf("Filling '%c' @ sent: %u, buflen: %u\n", fillChar, _sent, buflen);
    std::fill_n(buf, buflen, static_cast<uint8_t>(fillChar));
    _sent += buflen;
    fillChar = (fillChar == 'Z') ? 'A' : fillChar + 1;
    return buflen;
  }

private:
  char fillChar = 'A';
  size_t _sent = 0;
};

void setup() {
  Serial.begin(115200);

#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED || LT_ARD_HAS_WIFI || CONFIG_ESP32_WIFI_ENABLED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  // Example to use a AwsResponseFiller
  //
  // curl -v http://192.168.4.1/1 | grep -o '.' | sort | uniq -c
  //
  // Should output 16000 and the counts for each character from A to D
  //
  // Console:
  //
  // Filling 'A' @ index: 0, maxLen: 5652, toSend: 5652
  // Filling 'B' @ index: 5652, maxLen: 4308, toSend: 4308
  // Filling 'C' @ index: 9960, maxLen: 2888, toSend: 2888
  // Filling 'D' @ index: 12848, maxLen: 3152, toSend: 3152
  //
  server.on("/1", HTTP_GET, [](AsyncWebServerRequest *request) {
    fillChar = 'A';
    AsyncWebServerResponse *response = request->beginResponse("text/plain", totalResponseSize, [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      size_t remaining = totalResponseSize - index;
      size_t toSend = (remaining < maxLen) ? remaining : maxLen;
      Serial.printf("Filling '%c' @ index: %u, maxLen: %u, toSend: %u\n", fillChar, index, maxLen, toSend);
      std::fill_n(buffer, toSend, static_cast<uint8_t>(fillChar));
      fillChar = (fillChar == 'Z') ? 'A' : fillChar + 1;
      return toSend;
    });
    request->send(response);
  });

  // Example to use a AsyncAbstractResponse
  //
  // curl -v http://192.168.4.1/2 | grep -o '.' | sort | uniq -c
  //
  // Should output 16000
  //
  // Console:
  //
  // Filling 'A' @ sent: 0, buflen: 5675
  // Filling 'B' @ sent: 5675, buflen: 4308
  // Filling 'C' @ sent: 9983, buflen: 5760
  // Filling 'D' @ sent: 15743, buflen: 257
  //
  server.on("/2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(new CustomResponse());
  });

  server.begin();
}

void loop() {
  delay(100);
}
