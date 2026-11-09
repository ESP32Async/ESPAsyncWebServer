// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#include <ESPAsyncWebServer.h>

#include <utility>

AsyncWebHeader::ParseError AsyncWebHeader::parseError() const {
  return _name.length() ? ParseError::NONE : (_value.length() ? ParseError::ALLOC_FAIL : ParseError::INVALID);
}

const AsyncWebHeader AsyncWebHeader::parse(const char *data) {
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers
  // In HTTP/1.X, a header is a case-insensitive name followed by a colon, then optional whitespace which will be ignored, and finally by its value
  if (!data) {
    return AsyncWebHeader();  // nullptr
  }
  if (data[0] == '\0') {
    return AsyncWebHeader();  // empty string
  }
  if (strchr(data, '\n') || strchr(data, '\r')) {
    return AsyncWebHeader();  // Invalid header format
  }
  const char *colon = strchr(data, ':');
  if (!colon) {
    return AsyncWebHeader();  // separator not found
  }
  if (colon == data) {
    return AsyncWebHeader();  // Header name cannot be empty
  }
  const char *startOfValue = colon + 1;  // Skip the colon
  // RFC 7230 §3.2.3: OWS (optional whitespace) = *( SP / HTAB )
  // Skip all leading spaces and tabs, not just the first one.
  while (*startOfValue == ' ' || *startOfValue == '\t') {
    startOfValue++;
  }
  String name;
  if (!name.reserve(colon - data) || !name.concat(data, colon - data)) {
    async_ws_log_e("Failed to allocate while parsing header");
    // 'M' means NoMemory - no allocation => set in SSO buffer directly
    return AsyncWebHeader(String(), String('M'));
  }
  String value(startOfValue);
  // A copy of a non-empty value can never be empty: Arduino String leaves
  // length() == 0 only when the heap buffer could not be allocated (its
  // c_str() is then null on ESP32, which downstream header consumers such as
  // atoi() and strncasecmp() would dereference).  Report it so the caller can
  // fail the request.
  if (*startOfValue && !value.length()) {
    async_ws_log_e("Failed to allocate while parsing header");
    // 'M' means NoMemory - no allocation => set in SSO buffer directly
    return AsyncWebHeader(String(), String('M'));
  }
  return AsyncWebHeader(std::move(name), std::move(value));
}
