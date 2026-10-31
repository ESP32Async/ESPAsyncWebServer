// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#include <ESPAsyncWebServer.h>

#include <utility>

const AsyncWebHeader AsyncWebHeader::parse(const char *data) {
  AsyncWebHeader header;
  // Both a malformed line and an allocation failure yield an empty (invalid) header
  (void)tryParse(data, header);
  return header;
}

AsyncWebHeader::ParseStatus AsyncWebHeader::tryParse(const char *data, AsyncWebHeader &out) {
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers
  // In HTTP/1.X, a header is a case-insensitive name followed by a colon, then optional whitespace which will be ignored, and finally by its value
  if (!data) {
    return ParseStatus::Invalid;  // nullptr
  }
  if (data[0] == '\0') {
    return ParseStatus::Invalid;  // empty string
  }
  if (strchr(data, '\n') || strchr(data, '\r')) {
    return ParseStatus::Invalid;  // Invalid header format
  }
  const char *colon = strchr(data, ':');
  if (!colon) {
    return ParseStatus::Invalid;  // separator not found
  }
  if (colon == data) {
    return ParseStatus::Invalid;  // Header name cannot be empty
  }
  const char *startOfValue = colon + 1;  // Skip the colon
  // RFC 7230 §3.2.3: OWS (optional whitespace) = *( SP / HTAB )
  // Skip all leading spaces and tabs, not just the first one.
  while (*startOfValue == ' ' || *startOfValue == '\t') {
    startOfValue++;
  }
  String name;
  if (!name.reserve(colon - data) || !name.concat(data, colon - data)) {
    return ParseStatus::NoMemory;
  }
  String value(startOfValue);
  // A copy of a non-empty value can never be empty: Arduino String leaves
  // length() == 0 only when the heap buffer could not be allocated (its
  // c_str() is then null on ESP32, which downstream header consumers such as
  // atoi() and strncasecmp() would dereference).  Reject the header so the
  // caller can fail the request.
  if (*startOfValue && !value.length()) {
    return ParseStatus::NoMemory;
  }
  out._name = std::move(name);
  out._value = std::move(value);
  return ParseStatus::Ok;
}
