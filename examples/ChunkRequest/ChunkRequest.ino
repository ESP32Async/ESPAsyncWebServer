// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Mitch Bradley

//
// - Test for chunked encoding in requests
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
#include <LittleFS.h>

using namespace asyncsrv;

// Tests:
//
// Upload a file  with PUT
//   curl -T myfile.txt http://192.168.4.1/
//
// Upload a file with PUT using chunked encoding
//   curl -T bigfile.txt -H 'Transfer-Encoding: chunked' http://192.168.4.1/
//   ** Note: If the file will not fit in the available space, the server
//   ** does not know that in advance due to the lack of a Content-Length header.
//   ** The transfer will proceed until the filesystem fills up, then the transfer
//   ** will fail and the partial file will be deleted.  This works correctly with
//   ** recent versions (e.g. pioarduino) of the arduinoespressif32 framework, but
//   ** fails with the stale 3.20017.241212+sha.dcc1105b version due to a LittleFS
//   ** bug that has since been fixed.
//
// Immediately reject a chunked PUT that will not fit in available space
//   curl -T bigfile.txt -H 'Transfer-Encoding: chunked' -H 'X-Expected-Entity-Length: 99999999' http://192.168.4.1/
//   ** Note: MacOS WebDAVFS supplies the X-Expected-Entity-Length header with its
//   ** chunked PUTs

// This struct is used with _tempObject to communicate between handleBody and a subsequent handleRequest
struct RequestState {
  File outFile;
};

void handleRequest(AsyncWebServerRequest *request) {
  Serial.print(request->methodToString());
  Serial.print(" ");
  Serial.println(request->url());

  if (request->method() != HTTP_PUT) {
    request->send(400);  // Bad Request
    return;
  }

  // If request->_tempObject is not null, handleBody already
  // did the necessary work for a PUT operation
  auto state = static_cast<RequestState *>(request->_tempObject);
  if (state) {
    if (state->outFile) {
      // The file was already opened and written in handleBody so
      // we are done.  We will handle PUT without body data below.
      state->outFile.close();
      request->send(201);  // Created
    }
    delete state;
    request->_tempObject = nullptr;
    return;
  }

  String path = request->url();

  if (request->method() == HTTP_PUT) {
    // This PUT code executes if the body was empty, which
    // can happen if the client creates a zero-length file.
    // MacOS WebDAVFS does that, then later LOCKs the file
    // and issues a subsequent PUT with body contents.

#ifdef ESP32
    File file = LittleFS.open(path, "w", true);
#else
    File file = LittleFS.open(path, "w");
#endif

    if (file) {
      file.close();
      request->send(201);  // Created
      return;
    }
    request->send(403);
    return;
  }

  request->send(404);
}

void handleBody(AsyncWebServerRequest *request, unsigned char *data, size_t len, size_t index, size_t total) {
  if (request->method() == HTTP_PUT) {
    auto state = static_cast<RequestState *>(request->_tempObject);
    if (index == 0) {
      // parse the url to a proper path
      String path = request->url();

      state = new RequestState{File()};
      request->_tempObject = static_cast<void *>(state);

      if (total) {
#ifdef ESP32
        size_t avail = LittleFS.totalBytes() - LittleFS.usedBytes();
#else
        FSInfo info;
        LittleFS.info(info);
        auto avail = info.totalBytes - info.usedBytes;
#endif
        avail -= 4096;  // Reserve a block for overhead
        if (total > avail) {
          Serial.printf("PUT %d bytes will not fit in available space (%d).\n", total, avail);
          request->send(507, "text/plain", "Too large for available storage\r\n");
          return;
        }
      }
      Serial.print("Opening ");
      Serial.print(path);
      Serial.println(" from handleBody");
#ifdef ESP32
      File file = LittleFS.open(path, "w", true);
#else
      File file = LittleFS.open(path, "w");
#endif
      if (!file) {
        request->send(500, "text/plain", "Cannot create the file");
        return;
      }
      if (file.isDirectory()) {
        file.close();
        Serial.println("Cannot PUT to a directory");
        request->send(403, "text/plain", "Cannot PUT to a directory");
        return;
      }
      // If we already returned, the File object in request->_tempObject
      // is the default-contructed one.  The presence of

      std::swap(state->outFile, file);
      // Now request->_tempObject contains the actual file object which owns it,
      // and default-constructed File() object is in file, which will
      // go out of scope
    }
    if (state && state->outFile) {
      Serial.printf("write %d at %d\n", len, index);
      auto actual = state->outFile.write(data, len);
      if (actual != len) {
        Serial.println("WebDAV write failed.  Deleting file.");

        // Replace the File object in state with a null one
        File file{};
        std::swap(state->outFile, file);
        file.close();

        String path = request->url();
        LittleFS.remove(path);
        request->send(507, "text/plain", "Too large for available storage\r\n");
        return;
      }
    }
  }
}

static AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

#if ASYNCWEBSERVER_WIFI_SUPPORTED
#define AP_SUBNET 100
  IPAddress local_IP(192, 168, AP_SUBNET, 1);
  IPAddress gateway(192, 168, AP_SUBNET, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

#ifdef ESP32
  LittleFS.begin(true);
#else
  LittleFS.begin();
#endif

  server.onRequestBody(handleBody);
  server.onNotFound(handleRequest);

  server.begin();
}

void loop() {
  delay(100);
}
