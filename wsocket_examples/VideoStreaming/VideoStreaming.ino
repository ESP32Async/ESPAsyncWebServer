// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// MJPEG Cam Video streaming over WebSockets using AI-Thinker ESP32-CAM module
// you would need a module with camera to run this example https://www.espboards.dev/esp32/esp32cam/

/*
  This example implements a WebCam streaming in browser using cheap ESP32-cam module.
  The feature here is that frames are trasfered to the browser via WebSockets,
  it has several advantages over traditional streamed multipart content via HTTP
   - websockets delivers each frame in a separate message
   - webserver is not blocked when stream is flowing, you can still send/receive other data via HTTP
   - websockets can multiplex vide data and control messages, in this example you can also get memory
      stats / frame sizing from controller along with video stream
   - WSocketServer can easily replicate stream to multiple connected clients
      here in this example you can connect 2-3 clients simultaneously and get smooth stream (limited by wifi bandwidth)

  Change you WiFi creds below, build and flash the code.
  Connect to console to monitor for debug messages
  Figure out IP of your board and access the board via web browser, watch for video stream
*/

#include <AsyncWSocket.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "mjpeg.h"


#define WIFI_SSID "your_ssid"
#define WIFI_PASSWD "your_pass"

// AI-Thinker ESP32-CAM config - for more details see https://github.com/rzeldent/esp32cam-rtsp/ project
camera_config_t cfg {
  .pin_pwdn = 32,
  .pin_reset = -1,
  .pin_xclk = 0,

  .pin_sscb_sda = 26,
  .pin_sscb_scl = 27,

  // Note: LED GPIO is apparently 4 not sure where that goes
  // per https://github.com/donny681/ESP32_CAMERA_QR/blob/e4ef44549876457cd841f33a0892c82a71f35358/main/led.c
  .pin_d7 = 35,
  .pin_d6 = 34,
  .pin_d5 = 39,
  .pin_d4 = 36,
  .pin_d3 = 21,
  .pin_d2 = 19,
  .pin_d1 = 18,
  .pin_d0 = 5,
  .pin_vsync = 25,
  .pin_href = 23,
  .pin_pclk = 22,
  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_1,
  .ledc_channel = LEDC_CHANNEL_1,
  .pixel_format = PIXFORMAT_JPEG,
  // .frame_size = FRAMESIZE_UXGA, // needs 234K of framebuffer space
  // .frame_size = FRAMESIZE_SXGA, // needs 160K for framebuffer
  // .frame_size = FRAMESIZE_XGA, // needs 96K or even smaller FRAMESIZE_SVGA - can work if using only 1 fb
  .frame_size = FRAMESIZE_SVGA,
  .jpeg_quality = 15,               //0-63 lower numbers are higher quality
  .fb_count = 2, // if more than one i2s runs in continous mode.  Use only with jpeg
  .fb_location = CAMERA_FB_IN_PSRAM,
  .grab_mode = CAMERA_GRAB_LATEST,
  .sccb_i2c_port = 0
};

// camera frame buffer pointer
camera_fb_t *fb{nullptr};

// WS Event server callback declaration
void wsEvent(WSocketClient *client, WSocketClient::event_t event);

// Our WebServer
AsyncWebServer server(80);
// WSocket Server URL and callback function
WSocketServer ws("/wsstream", wsEvent);
// a unique steam id - it is used to avoid sending dublicate frames when multiple clients connected to stream
uint32_t client_id{0};

void sendFrame(uint32_t token){
  if (client_id == 0){
    // first client connected grabs the stream token
    client_id = token;
  } else if (token != client_id) {
    // we will send next frame only upon delivery the previous one to client owning the token, others we ignore
    // this is to avoid stacking frames clones in the Q when multiple clients are connected to the server
    return;
  }

  //return the frame buffer back to the driver for reuse
  esp_camera_fb_return(fb);
  // get 2nd buffer from camera
  fb = esp_camera_fb_get();

  // generate metadata info frame
  // this text frame contains memory stat and will be displayed along video stream
  char buff[100];
  snprintf(buff, 100, "FrameSize:%u, Mem:%lu, PSRAM:%lu", fb->len, ESP.getFreeHeap(), ESP.getFreePsram());
  ws.textAll(buff);

  // here we MUST ensure that client owning the stream is able to send data, otherwise recursion would crash controller
  if (ws.clientState(client_id) == WSocketClient::err_t::ok){
    /*    
      for video frame sending we will use WSMessageStaticBlob object.
      It can send large memory buffers directly to websocket peers without intermediate buffering and data copies
      and it is the most efficient way to send static data
    */
    auto m = std::make_shared<WSMessageStaticBlob>(
      WSFrameType_t::binary,    // binary message
      true,                     // final message
      reinterpret_cast<const char*>(fb->buf), fb->len, // buffer to transfer
      // the key here to understand when frame buffer completes delivery - for this we set
      // the callback back to ourself, so that when when frame delivery would be completed,
      // this function is called again to obtain a new frame buffer from camera
      [](WSMessageStatus_t s, uint32_t t){ sendFrame(t); }, // a callback executed on message delivery
      client_id     // stream token
    );
    // replicate frame to ALL peers
    ws.messageAll(m);
  } else {
    // current client can't receive stream (maybe he disconnected), we reset token here so that other client
    // can reconnect and take the ownership of the stream
    client_id = 0;
  }

  /*
    Note! Though this example is able to send video stream to multiple clients simultaneously, it has one gap -
    when same buffer is streamed to multiple peers and the 'owner' of stream completes transfer, others might
    still be in-progress. The buffer pointer is switched to next one from camera only upon full delivery on
    message object destruction. It does not allow pipelining and slower clients could affect the others.
    The question of synchronization multiple clients is out of scope of this simple example. It's just a
    demonstarion of working with WebSockets.
  */
}

void wsEvent(WSocketClient *client, WSocketClient::event_t event){
  switch (event){
    // new client connected
    case WSocketClient::event_t::connect : {
      Serial.printf("Client id:%lu connected\n", client->id);
      if (fb)
        sendFrame(client->id);
      else
        ws.text(client->id, "Cam init failed!");
      break;
    }

    // client diconnected
    case WSocketClient::event_t::disconnect : {
      Serial.printf("Client id:%lu disconnected\n", client->id);
      if (client_id == client->id)
      // reset stream token
      client_id = 0;
      break;
    }

    // any other events
    default:;
      // incoming messages could be used for controls or any other functionality
      // not implemented in this example but should be considered
      // If not discaded messages will overflow incoming Q
      client->dequeueMessage();
      break;
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
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.printf("to access VideoStream pls open http://%s/\n", WiFi.localIP().toString().c_str());

  // init camera
  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK)
  {
    Serial.printf("Camera probe failed with error 0x%x\n", err);
  } else 
    fb = esp_camera_fb_get();

  // server serves index page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // need to cast to uint8_t*
    // if you do not, the const char* will be copied in a temporary String buffer
    request->send(200, "text/html", (uint8_t *)htmlPage, std::string_view(htmlPage).length());
  });

  // attach our WSocketServer
  server.addHandler(&ws);
  server.begin();
}


void loop() {
  // nothing to do here at all
  vTaskDelete(NULL);
}
