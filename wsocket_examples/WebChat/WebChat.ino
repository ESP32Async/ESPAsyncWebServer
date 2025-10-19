// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// A simple WebChat room working over WebSockets
//

/*
  This example would show how WSocketServer could register connections/disconnections for every new user,
  deliver messages to all connected user and replicate incoming data amoung all members of chat room.
  Change you WiFi creds below, build and flash the code.
  Connect to console to monitor debug messages.
  Figure out IP of your board and access the board via web browser using two or more different devices,
  Chat with yourself, have fun :)
*/

#include <AsyncWSocket.h>
#include <WiFi.h>
#include "html.h"

#define WIFI_SSID "YourSSID"
#define WIFI_PASSWD "YourPasswd"

// WS Event server callback declaration
void wsEvent(WSocketClient *client, WSocketClient::event_t event);

// Our WebServer
AsyncWebServer server(80);
// WSocket Server URL and callback function
WSocketServer ws("/chat", wsEvent);

void wsEvent(WSocketClient *client, WSocketClient::event_t event){
  switch (event){
    // new client connected
    case WSocketClient::event_t::connect : {
      Serial.printf("Client id:%u connected\n", client->id);
      char buff[100];
      snprintf(buff, 100, "WServer: Hello user, your id is:%u, there are %u members on-line, pls be polite here!", client->id, ws.activeClientsCount());
      // greet new user personally
      ws.text(client->id, buff);
      snprintf(buff, 100, "WServer:  New client with id:%u joined the room", client->id);
      // Announce new user entered the room
      ws.textAll(client->id, buff);
      break;
    }

    // client diconnected
    case WSocketClient::event_t::disconnect : {
      Serial.printf("Client id:%u disconnected\n", client->id);
      char buff[100];
      snprintf(buff, 100, "WServer: Client with id:%u left the room, %u members on-line", client->id, ws.activeClientsCount());
      ws.textAll(client->id, buff);
      break;
    }

    // new messages from clients
    case WSocketClient::event_t::msgRecv :
      // any incoming messages we must deQ and discard,
      // there is no need to resend it back to, it's handled on the server side.
      // If not discaded messages will overflow incoming Q
      client->dequeueMessage();
      break;

    default:;
  }
}


void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Connected to WIFI_SSID");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.printf("to access WebChat pls open http://%s/\n", WiFi.localIP().toString().c_str());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // need to cast to uint8_t*
    // if you do not, the const char* will be copied in a temporary String buffer
    request->send(200, "text/html", (uint8_t *)htmlChatPage, strlen(htmlChatPage));
  });

  // keep TCP/WS connection open
  ws.setKeepAlive(20);

  /*
    Enable server echo to reflect incoming messages to all participans of the current chat room
  */
  ws.setServerEcho(true, true); // 2nd 'true' here is 'SplitHorizon' - server will reflect all message to every peer except the one where it came from

  server.addHandler(&ws);

  server.begin();
}


void loop() {
  // nothing to do here
  delay(100);
}
