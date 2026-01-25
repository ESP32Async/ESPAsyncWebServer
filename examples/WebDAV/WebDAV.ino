// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Mitch Bradley

//
// - WebDAV Server to access LittleFS files
// - Includes tests for chunked encoding in requests, as used
//   by MacOS WebDAVFS

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
// Get the OPTIONS that the WebDAV server supports
//   curl -v -X OPTIONS http://192.168.4.1
//   ** Note: The -v is necessary to see the option values because they
//   ** in the Allow: response header instead of the body.
// List a directory with PROPFIND
//   curl -X PROPFIND -H 'Depth: 1' http://192.168.4.1/
// List a directory with pretty-printed output
//   curl -X PROPFIND -H 'Depth: 1' http://192.168.4.1/ | xmllint --format -
// List a directory showing only the names
//   curl -X PROPFIND -H 'Depth: 1' http://192.168.4.1/ | xmllint --format - | grep href
// Upload a file  with PUT
//   curl -T myfile.txt http://192.168.4.1/
// Upload a file with PUT using chunked encoding
//   curl -T bigfile.txt -H 'Transfer-Encoding: chunked' http://192.168.4.1/
//   ** Note: If the file will not fit in the available space, the server
//   ** does not know that in advance due to the lack of a Content-Length header.
//   ** The transfer will proceed until the filesystem fills up, then the transfer
//   ** will fail and the partial file will be deleted.  This works correctly with
//   ** recent versions (e.g. pioarduino) of the arduinoespressif32 framework, but
//   ** fails with the stale 3.20017.241212+sha.dcc1105b version due to a LittleFS
//   ** bug that has since been fixed.
// Immediately reject a chunked PUT that will not fit in available space
//   curl -T bigfile.txt -H 'Transfer-Encoding: chunked' -H 'X-Expected-Entity-Length: 99999999' http://192.168.4.1/
//   ** Note: MacOS WebDAVFS supplies the X-Expected-Entity-Length header with its
//   ** chunked PUTs
// Download a file with GET (result to stdout)
//   curl http://192.168.4.1/myfile.txt
// Delete a file with DELETE
//   curl -X DELETE http://192.168.4.1/myfile.txt
// Create a subdirectory with MKCOL
//   curl -X MKCOL http://192.168.4.1/TheSubdir
// Upload a file to subdirectory
//   curl -T anotherfile.txt http://192.168.4.1/TheSubdir/
//   ** Note: without the trailing / the request will fail
// Lock a file against other access
//   curl -X LOCK http://192.168.4.1/myfile.txt
//   ** Note: Locks return a token but otherwise do nothing
//   ** other than making MacOS WebDAVFS happy
// Unlock a previously-locked file
//   curl -X UNLOCK http://192.168.4.1/myfile.txt
// Rename oldfile.txt to newfile.txt
//   curl -X MOVE -H 'Destination: http://192.168.4.1/newfile.txt' http://192.168.4.1/oldfile.txt

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

#ifdef ESP32
    File file = _fs.open(path, "w", true);
#else
    File file = _fs.open(path, "w");
#endif

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
    File file = _fs.open(path, "r");

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
    const AsyncWebHeader *destinationHeader = request->getHeader("Destination");
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
    File file = _fs.open(path, "r");

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
#ifdef ESP32
        size_t avail = LittleFS.totalBytes() - LittleFS.usedBytes();
#else
        FSInfo info;
        _fs.info(info);
        auto avail = info.totalBytes - info.usedBytes;
#endif
        avail -= 4096;  // Reserve a block for overhead
        if (total > avail) {
          Serial.printf("PUT %d bytes will not fit in available space (%d).\n", total, avail);
          request->send(507);  // Insufficient storage
          return;
        }
      }
      Serial.print("PUT: Opening ");
      Serial.println(path);

#ifdef ESP32
      File file = _fs.open(path, "w", true);
#else
      File file = _fs.open(path, "w");
#endif
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

  [[maybe_unused]]
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
  File file = _fs.open(path, "r");

  if (!file) {
    Serial.printf("%s not found\n", path.c_str());
    request->send(404);
    return;
  }

  AsyncWebServerResponse *response =
    request->beginResponse(getContentType(path), file.size(), [file, request](uint8_t *buffer, size_t maxLen, size_t filled) mutable -> size_t {
      if (!file) {
        request->client()->close();
        return 0;  //RESPONSE_TRY_AGAIN; // This only works for ChunkedResponse
      }

      int actual = 0;
      if (maxLen) {
        actual = file.read(buffer, maxLen);
      }
      if (actual == 0) {
        file.close();
      }
      return actual;
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
  File file = _fs.open(path, "r");
  bool is_dir = file.isDirectory();
  size_t size = file.size();

  // Just fake the time
  String timestr = "Fri, 05 Sep 2014 19:00:00 GMT";

  // send response
  String tag(path + timestr);

  sendItem(response, is_dir, path, tag, timestr, size);

  if (is_dir && level--) {
    String name;
#ifdef ESP32
    while ((name = file.getNextFileName()) != "") {
      sendPropResponse(response, level, name);
    }
#else
    File f;
    while ((f = file.openNextFile())) {
      sendPropResponse(response, level, f.name());
      f.close();
    }
#endif
  }
}

static AsyncWebServer server(80);
static AsyncHeaderFreeMiddleware *headerFilter;

void setup() {
  Serial.begin(115200);

#if ASYNCWEBSERVER_WIFI_SUPPORTED
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp-captive");
#endif

#ifdef ESP32
  LittleFS.begin(true);
  auto total = LittleFS.totalBytes();
  auto used = LittleFS.usedBytes();
#else
  LittleFS.begin();
  FSInfo info;
  LittleFS.info(info);
  auto total = info.totalBytes;
  auto used = info.usedBytes;
#endif

  Serial.print("LittleFS uses ");
  Serial.print(used);
  Serial.print(" of ");
  Serial.println(total);

  headerFilter = new AsyncHeaderFreeMiddleware();

  headerFilter->keep("Depth");
  headerFilter->keep("Destination");

  server.addMiddlewares({headerFilter});

  auto flash_dav = new WebDAV("/", LittleFS);
  server.addHandler(flash_dav);

  server.begin();
}

void loop() {
  delay(100);
}
