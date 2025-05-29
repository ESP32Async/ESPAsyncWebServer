// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#include "AsyncJson.h"

#if ASYNC_JSON_SUPPORT == 1

typedef struct {
  size_t length;  // the size we can write into "content", not including null termination
  uint8_t content
    [1];  // this will be of size "content-length" + 1 byte to guarantee that the content is null terminated. null termination is needed for ArduinoJson 5
} AsyncJsonResponseBuffer;

#if ARDUINOJSON_VERSION_MAJOR == 5
AsyncJsonResponse::AsyncJsonResponse(bool isArray) : _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_json;
  if (isArray) {
    _root = _jsonBuffer.createArray();
  } else {
    _root = _jsonBuffer.createObject();
  }
}
#elif ARDUINOJSON_VERSION_MAJOR == 6
AsyncJsonResponse::AsyncJsonResponse(bool isArray, size_t maxJsonBufferSize) : _jsonBuffer(maxJsonBufferSize), _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_json;
  if (isArray) {
    _root = _jsonBuffer.createNestedArray();
  } else {
    _root = _jsonBuffer.createNestedObject();
  }
}
#else
AsyncJsonResponse::AsyncJsonResponse(bool isArray) : _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_json;
  if (isArray) {
    _root = _jsonBuffer.add<JsonArray>();
  } else {
    _root = _jsonBuffer.add<JsonObject>();
  }
}
#endif

size_t AsyncJsonResponse::setLength() {
#if ARDUINOJSON_VERSION_MAJOR == 5
  _contentLength = _root.measureLength();
#else
  _contentLength = measureJson(_root);
#endif
  if (_contentLength) {
    _isValid = true;
  }
  return _contentLength;
}

size_t AsyncJsonResponse::_fillBuffer(uint8_t *data, size_t len) {
  ChunkPrint dest(data, _sentLength, len);
#if ARDUINOJSON_VERSION_MAJOR == 5
  _root.printTo(dest);
#else
  serializeJson(_root, dest);
#endif
  return len;
}

#if ARDUINOJSON_VERSION_MAJOR == 6
PrettyAsyncJsonResponse::PrettyAsyncJsonResponse(bool isArray, size_t maxJsonBufferSize) : AsyncJsonResponse{isArray, maxJsonBufferSize} {}
#else
PrettyAsyncJsonResponse::PrettyAsyncJsonResponse(bool isArray) : AsyncJsonResponse{isArray} {}
#endif

size_t PrettyAsyncJsonResponse::setLength() {
#if ARDUINOJSON_VERSION_MAJOR == 5
  _contentLength = _root.measurePrettyLength();
#else
  _contentLength = measureJsonPretty(_root);
#endif
  if (_contentLength) {
    _isValid = true;
  }
  return _contentLength;
}

size_t PrettyAsyncJsonResponse::_fillBuffer(uint8_t *data, size_t len) {
  ChunkPrint dest(data, _sentLength, len);
#if ARDUINOJSON_VERSION_MAJOR == 5
  _root.prettyPrintTo(dest);
#else
  serializeJsonPretty(_root, dest);
#endif
  return len;
}

#if ARDUINOJSON_VERSION_MAJOR == 6
AsyncCallbackJsonWebHandler::AsyncCallbackJsonWebHandler(const String &uri, ArJsonRequestHandlerFunction onRequest, size_t maxJsonBufferSize)
  : _uri(uri), _method(HTTP_GET | HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest(onRequest), maxJsonBufferSize(maxJsonBufferSize), _maxContentLength(16384) {}
#else
AsyncCallbackJsonWebHandler::AsyncCallbackJsonWebHandler(const String &uri, ArJsonRequestHandlerFunction onRequest)
  : _uri(uri), _method(HTTP_GET | HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest(onRequest), _maxContentLength(16384) {}
#endif

bool AsyncCallbackJsonWebHandler::canHandle(AsyncWebServerRequest *request) const {
  if (!_onRequest || !request->isHTTP() || !(_method & request->method())) {
    return false;
  }

  if (_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri + "/"))) {
    return false;
  }

  if (request->method() != HTTP_GET && !request->contentType().equalsIgnoreCase(asyncsrv::T_application_json)) {
    return false;
  }

  return true;
}

void AsyncCallbackJsonWebHandler::handleRequest(AsyncWebServerRequest *request) {
  if (_onRequest) {
    if (request->method() == HTTP_GET) {
      JsonVariant json;
      _onRequest(request, json);
      return;
    }
    // this is not a GET
    // check if json body is too large, if it is, don't deserialize
    if (request->contentLength() > _maxContentLength) {
      request->send(413);
      return;
    }

    // try to parse body as JSON
    if (request->_tempObject != NULL)  // see if we succeeded allocating a buffer earlier
    {
      AsyncJsonResponseBuffer *buffer = (AsyncJsonResponseBuffer *)request->_tempObject;
#if ARDUINOJSON_VERSION_MAJOR == 5
      DynamicJsonBuffer jsonBuffer;
      buffer->content[buffer->length] = '\0';  // null terminate, assume we allocated one extra char
      // parse can only get null terminated strings as parameters
      JsonVariant json = jsonBuffer.parse(buffer->content);
      if (json.success()) {
#elif ARDUINOJSON_VERSION_MAJOR == 6
      DynamicJsonDocument jsonBuffer(this->maxJsonBufferSize);  // content with length > this->maxJsonBufferSize might not get deserialized
      DeserializationError error = deserializeJson(jsonBuffer, buffer->content, buffer->length);
      if (!error) {
        JsonVariant json = jsonBuffer.as<JsonVariant>();
#else
      JsonDocument jsonBuffer;
      // deserializeJson expects a null terminated string or a pointer plus length
      DeserializationError error = deserializeJson(jsonBuffer, buffer->content, buffer->length);
      if (!error) {
        JsonVariant json = jsonBuffer.as<JsonVariant>();
#endif

        _onRequest(request, json);
        return;
      }
      // free buffer, we are done with it, so release memory ASAP
      free(request->_tempObject);
      request->_tempObject = NULL;
    }
    // there is no body, no buffer or we had an error parsing the body
    request->send(400);
  } else {  // if no _onRequest
    request->send(500);
  }
}

void AsyncCallbackJsonWebHandler::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (_onRequest) {
    if (index == 0)  // on first piece
    {
      // check nobody has already allocated the buffer
      if (request->_tempObject != NULL) {
#ifdef ESP32
        log_e("Temp object already in use");
#endif
        return;  // do nothing else here, handleRequest will return a HTTP error
      }
      // check total size is valid
      if (total >= _maxContentLength) {
        return;  // do nothing else here, handleRequest will return a HTTP error
      }
      // allocate buffer
      request->_tempObject = calloc(
        1, sizeof(AsyncJsonResponseBuffer) + total
      );                                   // normally _tempObject will be "free"ed  by the destructor of the request, but can release earlier if desired.
      if (request->_tempObject == NULL) {  // if allocation failed
#ifdef ESP32
        log_e("Failed to allocate");
#endif
        return;  // do nothing else here, handleRequest will return a HTTP error
      }
      ((AsyncJsonResponseBuffer *)request->_tempObject)->length = total;  // store the size of allocation we made into _tempObject
    }

    // add data to the buffer if the buffer exists
    if (request->_tempObject != NULL) {
      AsyncJsonResponseBuffer *buffer = (AsyncJsonResponseBuffer *)request->_tempObject;
      // check if the buffer is the right size so we don't write out of bounds
      if (buffer->length >= total && buffer->length >= index + len) {
        memcpy(buffer->content + index, data, len);
      } else {
#ifdef ESP32
        log_e("Bad size of temp buffer");
#endif
        return;  // do nothing else here
      }
    }
  }
}

#endif  // ASYNC_JSON_SUPPORT
