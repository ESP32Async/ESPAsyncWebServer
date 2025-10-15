// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

/*
  this example shows how to handle multiple websocket endpoints within same server instance

*/

#include <AsyncWSocket.h>
#include <WiFi.h>
#include "endpoints.h"

#define WIFI_SSID "your_ssid"
#define WIFI_PASSWD "your_pass"

// WebSocket endpoints to serve
constexpr const char WSENDPOINT_DEFAULT[] = "/ws";      // default endpoint - used for logging messages
constexpr const char WSENDPOINT_ECHO[] = "/wsecho";     // echo server - it replies back messages it receives
constexpr const char WSENDPOINT_SPEED[] = "/wsspeed";   // upstream speed test - sending large data chunks from server to browser

// *** Event handlers ***
// WS Events dispatcher
void wsEventDispatcher(WSocketClient *client, WSocketClient::event_t event);
// speed tester
void wsSpeedService(WSocketClient *client, WSocketClient::event_t event);
// echo service
void wsEchoService(WSocketClient *client, WSocketClient::event_t event);
// default service
void wsDefaultService(WSocketClient *client, WSocketClient::event_t event);

// Web Sever
AsyncWebServer server(80);
// WebSocket server instance
WSocketServer ws(WSENDPOINT_DEFAULT, wsEventDispatcher);

// this function is attached as main callback function for websocket events
void wsEventDispatcher(WSocketClient *client, WSocketClient::event_t event){
  if (event == WSocketClient::event_t::connect || event == WSocketClient::event_t::disconnect){
    // report all new connections to default endpoint
    char buff[100];
    snprintf(buff, 100, "Client %s, id:%lu, IP:%s:%u\n",
        event == WSocketClient::event_t::connect ? "connected" : "disconnected" ,
        client->id,
        IPAddress (client->client()->getRemoteAddress4().addr).toString().c_str(),
        client->client()->getRemotePort()
    );
    // send message to clients connected to default /ws endpoint
    ws.textToEndpoint(WSENDPOINT_DEFAULT, buff);
    Serial.print(buff);
  }

  // here we identify on which endpoint we received and event and dispatch to the corresponding handler
  switch (client->getURLHash())
  {
    case asyncsrv::hash_djb2a(WSENDPOINT_ECHO) :
      wsEchoService(client, event);
      break;

      case asyncsrv::hash_djb2a(WSENDPOINT_SPEED) :
      wsSpeedService(client, event);
      break;
    
    default:
      wsDefaultService(client, event);
      break;
  }

}


// default service - we will use it for event logging and information reports
void wsDefaultService(WSocketClient *client, WSocketClient::event_t event){
  switch (event){
    case WSocketClient::event_t::msgRecv : {
      // we do nothing but discard messages here (if any), no use for now
      client->dequeueMessage();
      break;
    }

    default:;
  }  
}

// default service - we will use it for event logging and information reports
void wsEchoService(WSocketClient *client, WSocketClient::event_t event){
  switch (event){
    case WSocketClient::event_t::connect : {
      ws.text(client->id, "Hello Client, this is an echo endpoint, message me something and I will reply it back");
      break;
    }

    // incoming message
    case WSocketClient::event_t::msgRecv : {
      auto m = client->dequeueMessage();
      if (m->type == WSFrameType_t::text){
        // got a text message, reformat it and reply
        std::string msg("Your message was: ");
        msg.append(m->getData());
        // avoid copy and move string to message queue
        ws.text(client->id, std::move(msg));
      }

      break;
    }
    default:;
  }  
}


// for speed test load
uint8_t *buff = nullptr;
size_t buff_size = 32 * 1024; // will send 32k message buffer
//size_t cnt{0};
// a unique stream id - it is used to avoid sending dublicate frames when multiple clients connected to speedtest endpoint
uint32_t client_id{0};

void bulkSend(uint32_t token){
  if (!buff) return;
  if (client_id == 0){
    // first client connected grabs the stream
    client_id = token;
  } else if (token != client_id) {
    // we will send next frame only upon delivery the previous one to client owning the stream, for others we ignore
    // this is to avoid stacking new frames in the Q when multiple clients are connected to the server
    return;
  }

  // generate metadata info frame
  // this text frame will carry our resources stat
  char msg[120];
  snprintf(msg, 120, "FrameSize:%u, Mem:%lu, psram:%lu, token:%lu", buff_size, ESP.getFreeHeap(), ESP.getFreePsram(), client_id);

  ws.textToEndpoint(WSENDPOINT_SPEED, msg);

  // here we MUST ensure that client owning the stream is able to send data, otherwise recursion would crash controller
  if (ws.clientState(client_id) == WSocketClient::err_t::ok){
    // for bulk load sending we will use WSMessageStaticBlob object, it will directly send
    // payload to websocket peers without intermediate buffer copies and
    // it is the most efficient way to send large objects from memory/ROM
    auto m = std::make_shared<WSMessageStaticBlob>(
      WSFrameType_t::binary,    // bynary message
      true,                     // final message
      reinterpret_cast<const char*>(buff), buff_size, // buffer to transfer
      // the key here to understand when frame buffer completes delivery - for this we set
      // the callback back to ourself, so that when when
      // this frame would complete delivery, this function is called again to obtain a new frame buffer from camera
      [](WSMessageStatus_t s, uint32_t t){ bulkSend(t); },
      client_id   // message token
    );
    // send message to all peers of this endpoint
    ws.messageToEndpoint(WSENDPOINT_SPEED, m);
    //++cnt;
  } else {
    client_id = 0;
  }
}

// speed tester - this endpoint will send bulk dummy payload to anyone who connects here
void wsSpeedService(WSocketClient *client, WSocketClient::event_t event){
  switch (event){
    case WSocketClient::event_t::connect : {
      // prepare a buffer with some junk data
      if (!buff)
        buff = (uint8_t*)malloc(buff_size);
      // start an endless bulk transfer
      bulkSend(client->id);
      break;
    }

    // incoming message
    case WSocketClient::event_t::msgRecv : {
      // silently discard here everything comes in
      client->dequeueMessage();
      }
      break;

    case WSocketClient::event_t::disconnect :
      // if no more clients are connected, release memory
      if ( ws.activeEndpointClientsCount(WSENDPOINT_SPEED) == 0){
        delete buff;
        buff = nullptr;
      }
      break;

    default:;
  }  

}



// setup our server
void setup() {
  Serial.begin(115200);

#ifndef CONFIG_IDF_TARGET_ESP32H2
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  //WiFi.softAP("esp-captive");
#endif
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  //Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Open the browser and connect to http://%s/\n", WiFi.localIP());

  // HTTP endpoint
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // need to cast to uint8_t*
    // if you do not, the const char* will be copied in a temporary String buffer
    request->send(200, "text/html", (uint8_t *)htmlPage, std::string_view(htmlPage).length());
  });

  // add endpoint for bulk speed testing
  ws.addURLendpoint("/wsspeed");

  // add endpoint for message echo testing
  ws.addURLendpoint("/wsecho");

  // attach WebSocket server to web server
  server.addHandler(&ws);

  // start server
  server.begin();

  //log_e("e setup end");
  //log_w("w server started");
  //log_d("d server debug");
}


void loop() {
  // nothing to do here
  vTaskDelete(NULL);
}
