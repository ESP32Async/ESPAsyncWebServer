// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// Shows how to use request continuation to pause a request for a long processing task, and be able to resume it later.
//

#include <Arduino.h>
#ifdef ESP32
#include <AsyncTCP.h>
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040)
#include <WebServer.h>
#include <WiFi.h>
#endif

#include <ESPAsyncWebServer.h>
#include <list>
#include <mutex>

static AsyncWebServer server(80);

// ===============================================================
// The code below is used to simulate some long running operations
// ===============================================================

// structure to hold info on long ops and paused requests
typedef struct {
  size_t id;
  AsyncWebServerRequestPtr requestPtr;
  uint8_t data;
} LongRunningOperation;

// list of all long-running operations
static std::list<LongRunningOperation *> longRunningOperations;
// ID counter of all long-running operations
static size_t longRunningOperationsCount = 0;
// mutex to protect the long-running operations list
static std::mutex longRunningOperationsMutex;

static void startLongRunningOperation(AsyncWebServerRequestPtr requestPtr) {
  std::lock_guard<std::mutex> lock(longRunningOperationsMutex);

  LongRunningOperation *op = new LongRunningOperation();
  op->id = ++longRunningOperationsCount;
  op->requestPtr = requestPtr;
  op->data = random(10, 30);

  Serial.printf("[%u] Start long running operation for %" PRIu8 " seconds...\n", op->id, op->data);
  longRunningOperations.push_back(op);
}

static bool processLongRunningOperation(LongRunningOperation *op) {
  op->data--;

  if (op->data) {
    // operation still running - print some status...
    Serial.printf("[%u] Long running operation will end in %" PRIu8 " seconds\n", op->id, op->data);
    return false;
  }

  // operation finished!

  // Try to get access to the request pointer if it is still exist.
  // If there has been a disconnection during that time, the pointer won't be valid anymore
  if (auto request = op->requestPtr.lock()) {
    Serial.printf("[%u] Long running operation finished! Sending back response for request..." PRIu8 " seconds\n", op->id);
    request->send(200, "text/plain", String("Long running operation ") + op->id + " finished.");

  } else {
    Serial.printf("[%u] Long running operation finished, but request was deleted!\n", op->id);
  }

  return true;
}

/// ==========================================================

void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  // Add a middleware to see how pausing a request affects the middleware chain
  server.addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
    Serial.printf("Middleware chain start\n");

    // continue to the next middleware, and at the end the request handler
    next();

    if (request->isPaused()) {
      Serial.printf("Request was paused!\n");
    }

    Serial.printf("Middleware chain end\n");
  });

  // HOW TO RUN THIS EXAMPLE:
  //
  // 1. Open several terminals to trigger some requests concurrently that will be paused with:
  //    > time curl -v http://192.168.4.1/
  //
  // 2. Look at the output of the Serial console to see how the middleware chain is executed and to see the long running operations being processed and resume the requests.
  //
  // 3. You can try disconnect the network or close your curl comand to disrupt the request and check that the request is deleted.
  //
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Print a message in case the request is disconnected (network disconnection, client close, etc.)
    request->onDisconnect([]() {
      Serial.printf("Request was disconnected!\n");
    });

    // Instruct ESPAsyncWebServer to pause the request and get a weak pointer to be able to access the request later.
    // The Middleware chain will continue to run until the end after this handler exit, but the request will be paused and not sent to the client until send() is called later.
    Serial.printf("Pausing request...\n");
    AsyncWebServerRequestPtr ptr = request->pause();

    // start our long operation...
    startLongRunningOperation(ptr);
  });

  server.begin();
}

static uint32_t lastTime = 0;

void loop() {
  if (millis() - lastTime >= 1000) {

#ifdef ESP32
    Serial.printf("Free heap: %" PRIu32 "\n", ESP.getFreeHeap());
#endif

    std::lock_guard<std::mutex> lock(longRunningOperationsMutex);

    // process all long running operations
    std::list<LongRunningOperation *> finished;
    for (auto it = longRunningOperations.begin(); it != longRunningOperations.end();) {
      bool done = processLongRunningOperation(*it);
      if (done) {
        finished.push_back(*it);
      }
    }

    // remove finished operations
    for (LongRunningOperation *op : finished) {
      Serial.printf("[%u] Deleting finished long running operation\n", op->id);
      longRunningOperations.remove(op);
      delete op;
    }

    lastTime = millis();
  }
}
