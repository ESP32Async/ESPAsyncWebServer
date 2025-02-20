// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// Authentication and authorization middlewares
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

// basicAuth
static AsyncAuthenticationMiddleware basicAuth;
static AsyncAuthenticationMiddleware basicAuthHash;

// simple digest authentication
static AsyncAuthenticationMiddleware digestAuth;
static AsyncAuthenticationMiddleware digestAuthHash;

// complex authentication which adds request attributes for the next middlewares and handler
static AsyncMiddlewareFunction complexAuth([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
  if (!request->authenticate("user", "password")) {
    return request->requestAuthentication();
  }

  // add attributes to the request for the next middlewares and handler
  request->setAttribute("user", "Mathieu");
  request->setAttribute("role", "staff");
  if (request->hasParam("token")) {
    request->setAttribute("token", request->getParam("token")->value().c_str());
  }

  next();
});

static AsyncAuthorizationMiddleware authz([](AsyncWebServerRequest *request) {
  return request->getAttribute("token") == "123";
});

void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

  // basic authentication
  basicAuth.setUsername("admin");
  basicAuth.setPassword("admin");
  basicAuth.setRealm("MyApp");
  basicAuth.setAuthFailureMessage("Authentication failed");
  basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
  basicAuth.generateHash();  // precompute hash (optional but recommended)

  // basic authentication with hash
  basicAuthHash.setUsername("admin");
  basicAuthHash.setPasswordHash("YWRtaW46YWRtaW4=");  // BASE64(admin:admin)
  basicAuthHash.setRealm("MyApp");
  basicAuthHash.setAuthFailureMessage("Authentication failed");
  basicAuthHash.setAuthType(AsyncAuthType::AUTH_BASIC);

  // digest authentication
  digestAuth.setUsername("admin");
  digestAuth.setPassword("admin");
  digestAuth.setRealm("MyApp");
  digestAuth.setAuthFailureMessage("Authentication failed");
  digestAuth.setAuthType(AsyncAuthType::AUTH_DIGEST);
  digestAuth.generateHash();  // precompute hash (optional but recommended)

  // digest authentication with hash
  digestAuthHash.setUsername("admin");
  digestAuthHash.setPasswordHash("f499b71f9a36d838b79268e145e132f7");  // MD5(user:realm:pass)
  digestAuthHash.setRealm("MyApp");
  digestAuthHash.setAuthFailureMessage("Authentication failed");
  digestAuthHash.setAuthType(AsyncAuthType::AUTH_DIGEST);

  // basic authentication method
  // curl -v -u admin:admin  http://192.168.4.1/auth-basic
  server
    .on(
      "/auth-basic", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Hello, world!");
      }
    )
    .addMiddleware(&basicAuth);

  // basic authentication method with hash
  // curl -v -u admin:admin  http://192.168.4.1/auth-basic-hash
  server
    .on(
      "/auth-basic-hash", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Hello, world!");
      }
    )
    .addMiddleware(&basicAuthHash);

  // digest authentication
  // curl -v -u admin:admin --digest  http://192.168.4.1/auth-digest
  server
    .on(
      "/auth-digest", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Hello, world!");
      }
    )
    .addMiddleware(&digestAuth);

  // digest authentication with hash
  // curl -v -u admin:admin --digest  http://192.168.4.1/auth-digest-hash
  server
    .on(
      "/auth-digest-hash", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Hello, world!");
      }
    )
    .addMiddleware(&digestAuthHash);

  // test digest auth custom authorization middleware
  // curl -v --digest -u user:password  http://192.168.4.1/auth-custom?token=123 => OK
  // curl -v --digest -u user:password  http://192.168.4.1/auth-custom?token=456 => 403
  // curl -v --digest -u user:FAILED  http://192.168.4.1/auth-custom?token=456 => 401
  server
    .on(
      "/auth-custom", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        String buffer = "Hello ";
        buffer.concat(request->getAttribute("user"));
        buffer.concat(" with role: ");
        buffer.concat(request->getAttribute("role"));
        request->send(200, "text/plain", buffer);
      }
    )
    .addMiddlewares({&complexAuth, &authz});

  server.begin();
}

// not needed
void loop() {
  delay(100);
}
