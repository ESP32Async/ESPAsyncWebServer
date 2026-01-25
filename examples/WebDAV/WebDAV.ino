// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov
// Copyright 2026 Mitch Bradley

//
// - WebDAV Server to access LittleFS files
// - Includes tests for chunked encoding in requests, as used
//   by MacOS WebDAVFS

#include <Arduino.h>
#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#include <WiFi.h>
#endif

#include <WiFiConfig.h>

#include <map>

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

using namespace asyncsrv;

// Tests:
// Note: The '-4' curl argument prevents long IPv6 mDNS resolution timeouts
//       The tests will work without it, but will take several seconds to start.
//       If you use the dotted decimal IP address, the -4 is unnecessary.
//
// Get the OPTIONS that the WebDAV server supports
//   curl -4 -v -X OPTIONS http://davtest.local
//   ** Note: The -v is necessary to see the option values because they
//   ** in the Allow: response header instead of the body.
// List a directory with PROPFIND
//   curl -4 -X PROPFIND -H 'Depth: 1' http://davtest.local/
// List a directory with pretty-printed output
//   curl -4 -X PROPFIND -H 'Depth: 1' http://davtest.local/ | xmllint --format -
// List a directory showing only the names
//   curl -4 -X PROPFIND -H 'Depth: 1' http://davtest.local/ | xmllint --format - | grep href
// Upload a file  with PUT
//   curl -4 -T myfile.txt http://davtest.local/
// Upload a file with PUT using chunked encoding
//   curl -4 -T bigfile.txt -H 'Transfer-Encoding: chunked' http://davtest.local/
//   ** Note: If the file will not fit in the available space, the server
//   ** does not know that in advance due to the lack of a Content-Length header.
//   ** The transfer will proceed until the filesystem fills up, then the transfer
//   ** will fail and the partial file will be deleted.
// Immediately reject a chunked PUT that will not fit in available space
//   curl -4 -T bigfile.txt -H 'Transfer-Encoding: chunked' -H 'X-Expected-Entity-Length: 99999999' http://davtest.local/
//   ** Note: MacOS WebDAVFS supplies the X-Expected-Entity-Length header with its
//   ** chunked PUTs
// Download a file with GET (result to stdout)
//   curl -4 http://davtest.local/myfile.txt
// Delete a file with DELETE
//   curl -4 -X DELETED http://davtest.local/myfile.txt
// Create a subdirectory with MKCOL
//   curl -4 -X MKCOL http://davtest.local/TheSubdir
// Upload a file to subdirectory
//   curl -4 -T anotherfile.txt http://davtest.local/TheSubdir/
//   ** Note: without the trailing / the request will fail
// Lock a file against other access
//   curl -4 -X LOCK http://davtest.local/myfile.txt
//   ** Note: Locks return a token but otherwise do nothing
//   ** other than making MacOS WebDAVFS happy
// Unlock a previously-locked file
//   curl -4 -X UNLOCK http://davtest.local/myfile.txt
// Rename oldfile.txt to newfile.txt
//   curl -4 -X MOVE -H 'Destination: http://davtest.local/newfile.txt' http://davtest.local/oldfile.txt

#ifndef ESP32
// this example is only for the ESP32
void setup() {}
void loop() {}
#else

struct mime_type {
  const char *suffix;
  const char *mime_type;
} mime_types[] = {
  {".htm", "text/html"},         {".html", "text/html"},
  {".css", "text/css"},          {".js", "application/javascript"},
  {".png", "image/png"},         {".gif", "image/gif"},
  {".jpeg", "image/jpeg"},       {".jpg", "image/jpeg"},
  {".ico", "image/x-icon"},      {".xml", "text/xml"},
  {".pdf", "application/x-pdf"}, {".zip", "application/x-zip"},
  {".gz", "application/x-gzip"}, {".txt", "text/plain"},
  {".gc", "text/plain"},         {".gcode", "text/plain"},
  {".nc", "text/plain"},         {"", "application/octet-stream"},
};

const char *getContentType(const String &filename) {
  String lcname(filename);
  lcname.toLowerCase();
  mime_type *m;
  for (m = mime_types; *(m->suffix) != '\0'; ++m) {
    if (lcname.endsWith(m->suffix)) {
      return m->mime_type;
    }
  }
  return m->mime_type;
}

struct RequestState {
  File outFile;
};

class WebDAV : public AsyncWebHandler {

public:
  WebDAV(const String &url, FS &volume);

  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;
  void handleBody(AsyncWebServerRequest *request, unsigned char *data, size_t len, size_t index, size_t total) override final;

  const char *url() const {
    return _url.c_str();
  }

private:
  String _url;
  FS &_fs;

  void handlePropfind(const String &path, AsyncWebServerRequest *request);
  void handleGet(const String &path, AsyncWebServerRequest *request);
  void handleNotFound(AsyncWebServerRequest *request);
  void sendPropResponse(AsyncResponseStream *response, int level, const String &path);

  void sendItem(AsyncResponseStream *response, bool is_dir, const String &name, const String &tag, const String &time, size_t size);

  String urlToUri(String url);

  bool acceptsType(AsyncWebServerRequest *request, const char *);
  bool acceptsEncoding(AsyncWebServerRequest *request, const char *);
};

WebDAV::WebDAV(const String &url, FS &fs) : _url(url), _fs(fs) {}

bool WebDAV::canHandle(AsyncWebServerRequest *request) const {
  return request->url().startsWith(_url.c_str());
}

static const char* rootname = "/";
static const char* slashstr = "/";
static const char  slash    = '/';

// Mac command to prevent .DS_Store files:
//  defaults write com.apple.desktopservices DSDontWriteNetworkStores -bool TRUE
// Mac metadata files:
//  .metadata_never_index_unless_rootfs
//  .metadata_never_index
//  .Spotlight-V100
//  .DS_Store
//  ._* (metadata for the file *)
//  .hidden

void WebDAV::handleRequest(AsyncWebServerRequest *request) {
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

  Serial.print(request->methodToString());
  Serial.print(" ");
  Serial.println(path);

  if (request->method() == HTTP_MKCOL) {
    // does the file/dir already exist?
    int status;
    if (_fs.exists(path)) {
      // Already exists
      // I think there is an "Overwrite: {T,F}" header; we should handle it
      request->send(405);
    } else {
      request->send(_fs.mkdir(path) ? 201 : 405);
    }
    return;
  }

  if (request->method() == HTTP_PUT) {
    // This PUT code executes if the body was empty, which
    // can happen if the client creates a zero-length file.
    // MacOS WebDAVFS does that, then later LOCKs the file
    // and issues a subsequent PUT with body contents.

    File file = _fs.open(path, FILE_WRITE, true);
    if (file) {
      file.close();
      request->send(201);  // Created
      return;
    }
    request->send(403);
    return;
  }

  if (request->method() == HTTP_OPTIONS) {
    AsyncWebServerResponse *response = request->beginResponse(200);
    response->addHeader("Dav", "1,2");
    response->addHeader("Ms-Author-Via", "DAV");
    response->addHeader("Allow", "PROPFIND,OPTIONS,DELETE,MOVE,HEAD,POST,PUT,GET");
    request->send(response);
    return;
  }

  // If we are not creating the resource it must already exist
  if (!_fs.exists(path)) {
    Serial.printf("%s does not exist\n", path.c_str());
    request->send(404, "Resource does not exist");
    return;
  }

  if (request->method() == HTTP_HEAD) {
    File file = _fs.open(path);

    // HEAD is like GET without a body, but with Content-Length
    AsyncWebServerResponse *response = request->beginResponse(200, getContentType(path), "");  // AsyncBasicResponse
    response->setContentLength(file.size());

    request->send(response);
    return;
  }

  if (request->method() == HTTP_GET) {
    return handleGet(path, request);
  }

  if (request->method() == HTTP_PROPFIND) {
    handlePropfind(path, request);
    return;
  }

  if (request->method() == HTTP_LOCK) {
    String lockroot("http://");
    lockroot += request->host();
    lockroot += path;

    AsyncResponseStream *response = request->beginResponseStream("application/xml; charset=utf-8");
    response->setCode(200);
    response->addHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");

    response->print("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    response->print("<D:prop xmlns:D=\"DAV:\">");
    response->print("<D:lockdiscovery>");
    response->print("<D:activelock>");
    response->print("<D:locktype><write/></D:locktype>");
    response->print("<D:lockscope><exclusive/></D:lockscope>");
    response->print("<D:locktoken><D:href>urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9</D:href></D:locktoken>");
    response->printf("<D:lockroot><D:href>%s</D:href></D:lockroot>", lockroot.c_str());
    response->print("<D:depth>infinity</D:depth>");
    response->printf("<D:owner><a:href xmlns:a=\"DAV:\">%s</a:href></D:owner>", "todo");
    response->print("<D:timeout>Second-3600</D:timeout>");
    response->print("</D:activelock>");
    response->print("</D:lockdiscovery>");
    response->print("</D:prop>");

    request->send(response);
    return;
  }
  if (request->method() == HTTP_UNLOCK) {
    request->send(204);  // No Content
    return;
  }
  if (request->method() == HTTP_MOVE) {
    const AsyncWebHeader *destinationHeader = request->getHeader("destination");
    if (!destinationHeader || destinationHeader->value().isEmpty()) {
      request->send(400, "text/plain", "Missing destination header");
      return;
    }

    // Should handle "Overwrite: {T,F}" header
    String newpath = urlToUri(destinationHeader->value());
    Serial.printf("Renaming %s to %s\n", path.c_str(), newpath.c_str());

    if (_fs.exists(newpath)) {
      Serial.printf("Destination file %s already exists\n", newpath.c_str());
      request->send(500, "text/plain", "Destination file exists");
    } else {
      if (_fs.rename(path, newpath)) {
        Serial.println("Rename succeeded");
        request->send(201);
      } else {
        Serial.println("Rename failed");
        request->send(500, "text/plain", "Unable to move");
      }
    }

    return;
  }
  if (request->method() == HTTP_DELETE) {
    // delete file or dir
    bool result;
    File file = _fs.open(path);

    if (!file) {
      request->send(404);
    } else {
      if (file.isDirectory()) {
        file.close();
        request->send(_fs.rmdir(path) ? 200 : 201);
      } else {
        file.close();
        request->send(_fs.remove(path) ? 200 : 201);
      }
    }
    return;
  }

  handleNotFound(request);
}

    void WebDAV::handleBody(AsyncWebServerRequest *request, unsigned char *data, size_t len, size_t index, size_t total) {
      // The other requests with a body are LOCK and PROPFIND, where the body data is the XML
      // schema for their replies.  For now, we just ignore that data and hardcode the reply
      // schema.  It might be useful to decode the schema to, for example, omit some reply fields,
      // but that doesn't appear to be necessary at the moment.
      if (request->method() == HTTP_PUT) {
        auto state = static_cast<RequestState *>(request->_tempObject);
        if (index == 0) {
          // parse the url to a proper path
          String path = request->url();

          state = new RequestState{File()};
          request->_tempObject = static_cast<void *>(state);

          if (total) {
            // Ideally this would be _fs.totalBytes() - _fs.usedBytes()
            // but the Arduino FS class does not have totalBytes()
            // and usedBytes().  They exist in the SDFS and LittleFS
            // classes, but with different return types (uint64_t for
            // SDFS and uint32_t for LittleFS).
            // This should be abstracted somewhere but I have to draw
            // the line somewhere with this test code.
            size_t avail = LittleFS.totalBytes() - LittleFS.usedBytes();
            avail -= 4096;  // Reserve a block for overhead
            if (total > avail) {
              Serial.printf("PUT %d bytes will not fit in available space (%d).\n", total, avail);
              request->send(507);  // Insufficient storage
              return;
            }
          }
          Serial.print("PUT: Opening ");
          Serial.println(path);

          File file = _fs.open(path, FILE_WRITE, true);
          if (!file) {
            request->send(500);
            return;
          }
          if (file.isDirectory()) {
            file.close();
            Serial.println("Cannot PUT to a directory");
            request->send(403);
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
            _fs.remove(path);
            request->send(507);  // Insufficient storage
            return;
          }
        }
      }
    }

    bool WebDAV::acceptsEncoding(AsyncWebServerRequest *request, const char *encoding) {
      if (request->hasHeader("Accept-Encoding")) {
        auto encodings = String(request->getHeader("Accept-Encoding")->value().c_str());
        return encodings.indexOf(encoding) != -1;
      }
      return false;
    }

bool WebDAV::acceptsType(AsyncWebServerRequest *request, const char *type) {
  if (request->hasHeader(T_ACCEPT)) {
    auto types = String(request->getHeader(T_ACCEPT)->value().c_str());
    return types.indexOf(type) != -1;
  }
  return false;
}

void WebDAV::handlePropfind(const String &path, AsyncWebServerRequest *request) {
  auto depth = 0;

  bool noroot = false;

  const AsyncWebHeader *depthHeader = request->getHeader("Depth");
  if (depthHeader) {
    if (depthHeader->value().equals("1")) {
      depth = 1;
    } else if (depthHeader->value().equals("1,noroot")) {
      depth = 1;
      noroot = true;
    } else if (depthHeader->value().equals("infinity")) {
      depth = 99999;
    } else if (depthHeader->value().equals("infinity,noroot")) {
      depth = 99999;
      noroot = true;
    }
  }

  AsyncResponseStream *response = request->beginResponseStream("application/xml");
  response->setCode(207);

  response->print("<?xml version=\"1.0\"?>");
  response->print("<d:multistatus xmlns:d=\"DAV:\">");

  sendPropResponse(response, (int)depth, path);

  response->print("</d:multistatus>");

  request->send(response);
  return;
}

void WebDAV::handleGet(const String &path, AsyncWebServerRequest *request) {
  File file = _fs.open(path);

  if (!file) {
    Serial.printf("%s not found\n", path.c_str());
    request->send(404);
    return;
  }

  AsyncWebServerResponse *response =
    request->beginResponse(getContentType(path), file.size(), [file, request](uint8_t *buffer, size_t maxLen, size_t total) mutable -> size_t {
      if (!file) {
        request->client()->close();
        return 0;  //RESPONSE_TRY_AGAIN; // This only works for ChunkedResponse
      }
      if (total >= file.size() || request->method() != HTTP_GET) {
        file.close();
        return 0;
      }
      int bytes = min(file.size(), maxLen);
      int actual = file.read(buffer, bytes);  // return 0 even when no bytes were loaded
      if (bytes == 0 || (bytes + total) >= file.size()) {
        file.close();
      }
      return bytes;
    });

  request->onDisconnect([request, file]() mutable {
    file.close();
  });

  request->send(response);
}

void WebDAV::handleNotFound(AsyncWebServerRequest *request) {
  request->send(404);
}

String WebDAV::urlToUri(String url) {
  String uri(url);
  if (uri.startsWith("http://")) {
    uri = uri.substring(7);
  } else if (uri.startsWith("https://")) {
    uri = uri.substring(8);
  }
  // Now remove the hostname.

  auto pos = uri.indexOf('/');
  if (pos != -1) {
    uri = uri.substring(pos);
  }
  return uri;
}

void WebDAV::sendItem(AsyncResponseStream *response, bool is_dir, const String &name, const String &tag, const String &time, size_t size) {
  response->printf("<d:response>");
  response->printf("<d:href>%s</d:href>", name.c_str());
  response->printf("<d:propstat>");
  response->printf("<d:prop>");
  if (is_dir) {
    response->printf("<d:resourcetype><d:collection/></d:resourcetype>");
  } else {
    // response->printf("<d:getetag>%s</d:getetag>", tag.c_str());
    response->printf("<d:getlastmodified>%s</d:getlastmodified>", time.c_str());
    response->printf("<d:resourcetype/>");
    response->printf("<d:getcontentlength>%d</d:getcontentlength>", size);
    response->printf("<d:getcontenttype>%s</d:getcontenttype>", getContentType(name));
  }
  response->printf("</d:prop>");
  response->printf("<d:status>HTTP/1.1 200 OK</d:status>");
  response->printf("</d:propstat>");
  response->printf("</d:response>");
}

void WebDAV::sendPropResponse(AsyncResponseStream *response, int level, const String &path) {
  File file = _fs.open(path);
  bool is_dir = file.isDirectory();
  size_t size = file.size();

  // Just fake the time
  String timestr = "Fri, 05 Sep 2014 19:00:00 GMT";

  // send response
  String tag(path + timestr);

  sendItem(response, is_dir, path, tag, timestr, size);

  if (is_dir && level--) {
    String name;
    while ((name = file.getNextFileName()) != "") {
      sendPropResponse(response, level, name);
    }
  }
}

static AsyncWebServer server(80);
static AsyncHeaderFreeMiddleware *headerFilter;

void setup() {
  Serial.begin(115200);
  Serial.println("Setting up WiFi");

#ifdef STA_SSID
  Serial.println("Using STA mode");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(STA_SSID, STA_PASSWORD);
  Serial.print("Connecting to WiFi ..");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print('.');
  }
  Serial.println(WiFi.localIP());

#else
  IPAddress local_IP(192, 168, AP_SUBNET, 1);
  IPAddress gateway(192, 168, AP_SUBNET, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  Serial.print("Starting AP ");
  Serial.print(AP_SSID);
  Serial.print(" ");
  Serial.println(WiFi.softAPIP());
#endif

#ifdef MDNS_NAME
  // Initialize mDNS
  if (!MDNS.begin(MDNS_NAME)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.printf("mDNS on: %s.local\n", MDNS_NAME);

  // Add service to mDNS
  MDNS.addService("http", "tcp", 80);
#endif

  headerFilter = new AsyncHeaderFreeMiddleware();

  headerFilter->keep("Depth");
  headerFilter->keep("Destination");

  server.addMiddlewares({headerFilter});

  LittleFS.begin(true);
  Serial.print("LittleFS uses ");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" of ");
  Serial.println(LittleFS.totalBytes());

  auto flash_dav = new WebDAV("/", LittleFS);
  server.addHandler(flash_dav);

  //  server.on("/", HTTP_ANY, handle_roo);

  server.begin();
}

void loop() {
  delay(100);
}

#endif
