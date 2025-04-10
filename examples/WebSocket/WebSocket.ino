// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// WebSocket example
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
static AsyncWebSocket ws("/ws");

void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  //
  // Run in terminal 1: websocat ws://192.168.4.1/ws => should stream data
  // Run in terminal 2: websocat ws://192.168.4.1/ws => should stream data
  // Run in terminal 3: websocat ws://192.168.4.1/ws => should fail:
  //
  // To send a message to the WebSocket server:
  //
  // echo "Hello!" | websocat ws://192.168.4.1/ws
  //
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    (void)len;

    if (type == WS_EVT_CONNECT) {
      ws.textAll("new client connected");
      Serial.println("ws connect");
      client->setCloseClientOnQueueFull(false);
      client->ping();

    } else if (type == WS_EVT_DISCONNECT) {
      ws.textAll("client disconnected");
      Serial.println("ws disconnect");

    } else if (type == WS_EVT_ERROR) {
      Serial.println("ws error");

    } else if (type == WS_EVT_PONG) {
      Serial.println("ws pong");

    } else if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      Serial.printf("index: %" PRIu64 ", len: %" PRIu64 ", final: %" PRIu8 ", opcode: %" PRIu8 "\n", info->index, info->len, info->final, info->opcode);
      String msg = "";
      if (info->final && info->index == 0 && info->len == len) {
        if (info->opcode == WS_TEXT) {
          data[len] = 0;
          Serial.printf("ws text: %s\n", (char *)data);
        }
      }
    }
  });

  // shows how to prevent a third WS client to connect
  server.addHandler(&ws).addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
    // ws.count() is the current count of WS clients: this one is trying to upgrade its HTTP connection
    if (ws.count() > 1) {
      // if we have 2 clients or more, prevent the next one to connect
      request->send(503, "text/plain", "Server is busy");
    } else {
      // process next middleware and at the end the handler
      next();
    }
  });

  server.addHandler(&ws);

  server.begin();
}

static uint32_t lastWS = 0;
static uint32_t deltaWS = 100;

static uint32_t lastHeap = 0;

void loop() {
  uint32_t now = millis();

  if (now - lastWS >= deltaWS) {
    ws.printfAll("kp%.4f", (10.0 / 3.0));
    lastWS = millis();
  }

  if (now - lastHeap >= 2000) {
    Serial.printf("Connected clients: %u / %u total\n", ws.count(), ws.getClients().size());

    // this can be called to also set a soft limit on the number of connected clients
    ws.cleanupClients(2); // no more than 2 clients

#ifdef ESP32
    Serial.printf("Free heap: %" PRIu32 "\n", ESP.getFreeHeap());
#endif
    lastHeap = now;
  }
}
