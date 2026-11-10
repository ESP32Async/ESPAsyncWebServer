// Test doubles for response state and the AsyncClient add/write contract.
// Production method bodies are read from src/, never duplicated here.
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
using String = std::string;
#define ASYNC_WRITE_FLAG_COPY       1
#define CONFIG_LWIP_TCP_MSS         536
#define CONFIG_LWIP_TCP_WND_DEFAULT 2144
#define ASYNC_RESPONCE_BUFF_SIZE    1072
#ifndef TEST_TCP_DEFAULT_FLAGS
#define TEST_TCP_DEFAULT_FLAGS 0
#endif
#define async_ws_log_e(...) \
  do {                      \
  } while (0)
constexpr size_t RESPONSE_TRY_AGAIN = std::numeric_limits<size_t>::max();
enum {
  RESPONSE_SETUP,
  RESPONSE_HEADERS,
  RESPONSE_CONTENT,
  RESPONSE_WAIT_ACK,
  RESPONSE_END,
  RESPONSE_FAILED
};
struct Client {
  struct Pending {
    const char *data;
    size_t length;
    std::string copy;
  };
  size_t quota = 1072, sends = 0;
  bool deferred = false, closed = false;
  std::string wire;
  std::vector<Pending> pending;
  size_t space() const {
    return 1072;
  }
  size_t add(const char *data, size_t length, uint8_t flags = TEST_TCP_DEFAULT_FLAGS) {
    const size_t accepted = std::min(length, quota);
    quota -= accepted;
    if (accepted) {
      if (deferred) {
        pending.push_back({data, accepted, (flags & ASYNC_WRITE_FLAG_COPY) ? std::string(data, accepted) : std::string()});
      } else {
        wire.append(data, accepted);
      }
    }
    return accepted;
  }
  bool send() {
    ++sends;
    return true;
  }
  size_t write(const char *data, size_t length, uint8_t flags = TEST_TCP_DEFAULT_FLAGS) {
    const size_t accepted = add(data, length, flags);
    send();
    return accepted;
  }
  void close() {
    closed = true;
  }
  template<typename Callback> void onAck(Callback, void *) {}
  void flush() {
    for (const auto &p : pending) {
      const char *data = p.copy.empty() ? p.data : p.copy.data();
      for (size_t i = 0; i < p.length; ++i) {
        wire.push_back(data[i]);
      }
    }
    pending.clear();
  }
};
using AsyncClient = Client;
struct AsyncWebServerRequest {
  Client tcp;
  unsigned failures = 0;
  Client *client() {
    return &tcp;
  }
  Client *clientRelease() {
    return &tcp;
  }
  unsigned version() const {
    return 1;
  }
  void sendRawResponse(const char *) {
    ++failures;
  }
  bool hasHeader(const char *) const {
    return false;
  }
  struct Header {
    String value() const {
      return "0";
    }
  } header;
  Header *getHeader(const char *) {
    return &header;
  }
};
struct AsyncAbstractResponse {
  size_t _ackedLength = 0, _writtenLength = 0, _writtenHeadersLength = 0, _sentLength = 0, _contentLength = 0;
  size_t _in_flight = 0;
  unsigned _in_flight_credit = 2;
  bool _chunked = false, _sendContentLength = true;
  int _state = RESPONSE_WAIT_ACK;
  String _assembled_headers;
  std::unique_ptr<std::array<uint8_t, ASYNC_RESPONCE_BUFF_SIZE>> _send_buffer;
  size_t _send_buffer_len = 0, _send_buffer_offset = 0;
  bool _sourceValid() const {
    return true;
  }
  size_t _fillBufferAndProcessTemplates(uint8_t *, size_t) {
    return 0;
  }
  size_t write_send_buffs(AsyncWebServerRequest *, size_t, uint32_t);
};
