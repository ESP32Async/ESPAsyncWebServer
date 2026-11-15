// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#pragma once

#include <Arduino.h>

#if defined(ESP32) || defined(LIBRETINY) || defined(HOST)
#include <AsyncTCP.h>
#ifdef LIBRETINY
#ifdef round
#undef round
#endif
#endif
#include <mutex>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 32
#endif
#define SSE_MIN_INFLIGH 2 * 1460   // allow 2 MSS packets
#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q
#elif defined(ESP8266)
#include <ESPAsyncTCP.h>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 8
#endif
#define SSE_MIN_INFLIGH 2 * 1460  // allow 2 MSS packets
#define SSE_MAX_INFLIGH 8 * 1024  // but no more than 8k, no need to blow it, since same data is kept in local Q
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 32
#endif
#define SSE_MIN_INFLIGH 2 * 1460   // allow 2 MSS packets
#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q
#endif

#include <ESPAsyncWebServer.h>

#ifdef ESP8266
#include <Hash.h>
#ifdef CRYPTO_HASH_h  // include Hash.h from espressif framework if the first include was from the crypto library
#include <../src/Hash.h>
#endif
#endif

#include <functional>
#include <list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

class AsyncEventSource;
class AsyncEventSourceResponse;
class AsyncEventSourceClient;
// Ownership-safe handle to an SSE client connection.
// The handle keeps the client object alive even after it disconnected, and can
// be retained past any callback; use it as a stable connection identifier.
using AsyncEventSourceClientPtr = std::shared_ptr<AsyncEventSourceClient>;
using ArEventHandlerFunction = std::function<void(AsyncEventSourceClient *client)>;
// Stored callback forms; onConnect()/onDisconnect() adapt narrower callables into these.
using ArEventConnectHandler = std::function<void(AsyncWebServerRequest *request, const AsyncEventSourceClientPtr &client)>;
using ArEventDisconnectHandler = std::function<void(const AsyncEventSourceClientPtr &client)>;
using ArAuthorizeConnectHandler = ArAuthorizeFunction;
// shared message object container
using AsyncEvent_SharedData_t = std::shared_ptr<String>;

/**
 * @brief class holds a sse messages queue for a particular client's connection
 *
 */
class AsyncEventSourceClient : public std::enable_shared_from_this<AsyncEventSourceClient> {
private:
  friend class AsyncEventSource;
  friend class AsyncEventSourceResponse;
  AsyncClient *_client;
  AsyncEventSource *_server;
  uint32_t _lastId{0};
  size_t _inflight{0};                    // num of unacknowledged bytes that has been written to socket buffer
  size_t _max_inflight{SSE_MAX_INFLIGH};  // max num of unacknowledged bytes that could be written to socket buffer
  bool _ack_pending = false;
  size_t _sent{0};  // num of bytes already sent in the head message
  std::list<AsyncEvent_SharedData_t> _messageQueue;
  mutable asyncsrv::mutex_type _lockmq;
  bool _queueMessage(AsyncEvent_SharedData_t &&msg);
  void _runQueue();
  // bind AsyncTCP callbacks (ack/poll/timeout/disconnect) to this client;
  // called by AsyncEventSource::_addClient() once the client is shared-owned
  void _attach();
  // detach from the owning server (used when the server dies before the client)
  // and close the connection
  void _detach();
  void _onAck(size_t len, uint32_t time);
  void _onPoll();
  void _onTimeout(uint32_t time);
  void _onDisconnect();

  // Created by AsyncEventSourceResponse::_respond() without a connection;
  // AsyncEventSource::_addClient() adopts the request's and lists the client.
  AsyncEventSourceClient(AsyncClient *client, AsyncEventSource *server, uint32_t lastId = 0);

public:
  ~AsyncEventSourceClient();

  /**
     * @brief Send an SSE message to client
     * it will craft an SSE message and place it to client's message queue
     *
     * @param message body string, could be single or multi-line string sepprated by \n, \r, \r\n
     * @param event body string, a sinle line string
     * @param id sequence id
     * @param reconnect client's reconnect timeout
     * @return true if message was placed in a queue
     * @return false if queue is full
     */
  bool send(const char *message, const char *event = nullptr, uint32_t id = 0, uint32_t reconnect = 0);
  bool send(const String &message, const String &event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event.c_str(), id, reconnect);
  }
  bool send(const String &message, const char *event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event, id, reconnect);
  }

  /**
     * @brief place supplied preformatted SSE message to the message queue
     * @note message must a properly formatted SSE string according to https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events
     *
     * @param message data
     * @return true on success
     * @return false on queue overflow or no client connected
     */
  bool write(AsyncEvent_SharedData_t message) {
    return _queueMessage(std::move(message));
  };

  [[deprecated("Use write(AsyncEvent_SharedData_t message) instead to share same data with multiple SSE clients")]]
  bool write(const char *message, size_t len) {
    if (!connected()) {
      return false;
    }
    // Portable String construction - not all platforms have String(char*, size_t)
#if defined(ESP32)
    return _queueMessage(std::make_shared<String>(message, len));
#else
    auto data = std::make_shared<String>();
    if (message && len) {
      data->concat(message, len);
    }
    return _queueMessage(std::move(data));
#endif
  };

  // close client's connection
  void close();

  // getters

  AsyncClient *client() {
    return _client;
  }
  bool connected() const {
    return _client && _client->connected();
  }
  uint32_t lastId() const {
    return _lastId;
  }
  size_t packetsWaiting() const {
    asyncsrv::lock_guard_type lock(_lockmq);
    return _messageQueue.size();
  };

  /**
     * @brief Sets max amount of bytes that could be written to client's socket while awaiting delivery acknowledge
     * used to throttle message delivery length to tradeoff memory consumption
     * @note actual amount of data written could possible be a bit larger but no more than available socket buff space
     *
     * @param value
     */
  void set_max_inflight_bytes(size_t value);

  /**
     * @brief Get current max inflight bytes value
     *
     * @return size_t
     */
  size_t get_max_inflight_bytes() const {
    return _max_inflight;
  }
};

/**
 * @brief a class that maintains all connected HTTP clients subscribed to SSE delivery
 * dispatches supplied messages to the client's queues
 *
 */
class AsyncEventSource : public AsyncWebHandler {
private:
  friend class AsyncEventSourceClient;
  friend class AsyncEventSourceResponse;
  String _url;
  std::list<AsyncEventSourceClientPtr> _clients;
  // Same as for individual messages, protect mutations of _clients list
  // since simultaneous access from different tasks is possible
  mutable asyncsrv::mutex_type _client_queue_lock;
  ArEventConnectHandler _connectcb;
  ArEventDisconnectHandler _disconnectcb;

  // this method manipulates in-fligh data size for connected client depending on number of active connections
  void _adjust_inflight_window();

  // gives a pre-allocated client the request's connection, then registers it
  void _addClient(const AsyncEventSourceClientPtr &client, AsyncWebServerRequest *request);
  void _handleDisconnect(AsyncEventSourceClient *client);

public:
  typedef enum {
    DISCARDED = 0,
    ENQUEUED = 1,
    PARTIALLY_ENQUEUED = 2,
  } SendStatus;

  AsyncEventSource(const char *url) : _url(url){};
  AsyncEventSource(const String &url) : _url(url){};
  // Detaches clients still connected at destruction time so any app-held
  // handles survive without touching the dead server (see AsyncEventSource.cpp)
  ~AsyncEventSource();

  const char *url() const {
    return _url.c_str();
  }
  // close all connected clients
  void close();

  /**
     * @brief set on-connect callback for the client
     * used to deliver messages to client on first connect
     * @param cb callable as void(AsyncEventSourceClient *),
     *           void(const AsyncEventSourceClientPtr &), or
     *           void(AsyncWebServerRequest *, const AsyncEventSourceClientPtr &);
     *           a callable accepting several of these receives the last form.
     *           A raw pointer or the request is valid only during the callback;
     *           the shared handle may be stored.
     */
  template<typename F> void onConnect(F &&cb);
  void onConnect(std::nullptr_t) {
    _connectcb = nullptr;
  }

  /**
     * @brief set on-disconnect callback for the client
     * @param cb callable as void(AsyncEventSourceClient *) or
     *           void(const AsyncEventSourceClientPtr &);
     *           a raw pointer is valid only during the callback
     */
  template<typename F> void onDisconnect(F &&cb);
  void onDisconnect(std::nullptr_t) {
    _disconnectcb = nullptr;
  }

  /**
     * @brief Send an SSE message to client
     * it will craft an SSE message and place it to all connected client's message queues
     *
     * @param message body string, could be single or multi-line string sepprated by \n, \r, \r\n
     * @param event body string, a sinle line string
     * @param id sequence id
     * @param reconnect client's reconnect timeout
     * @return SendStatus if message was placed in any/all/part of the client's queues
     */
  SendStatus send(const char *message, const char *event = nullptr, uint32_t id = 0, uint32_t reconnect = 0);
  SendStatus send(const String &message, const String &event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event.c_str(), id, reconnect);
  }
  SendStatus send(const String &message, const char *event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event, id, reconnect);
  }

  void authorizeConnect(ArAuthorizeConnectHandler cb);

  // returns number of connected clients
  size_t count() const;

  // returns average number of messages pending in all client's queues
  size_t avgPacketsWaiting() const;

  /**
     * @brief Get a snapshot of all registered clients as shared handles.
     * The returned handles stay valid while held, even if a client disconnects
     * mid-iteration (it merely reports connected() == false). Use for targeted
     * sends or closes without retaining unsafe raw pointers.
     */
  std::vector<AsyncEventSourceClientPtr> getClients() const;

  bool canHandle(AsyncWebServerRequest *request) const final;
  void handleRequest(AsyncWebServerRequest *request) final;
};

class AsyncEventSourceResponse : public AsyncWebServerResponse {
private:
  friend class AsyncEventSource;
  AsyncEventSource *_server;
  AsyncEventSourceResponse(AsyncEventSource *server);

public:
  void _respond(AsyncWebServerRequest *request) override;
  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time) override {
    return 0;
  };
  bool _sourceValid() const override {
    return true;
  }
};

namespace asyncsrv {
namespace detail {

// C++11 stand-in for std::is_invocable: is an lvalue G callable with Args?
template<typename G, typename... Args> auto callable_test(int) -> decltype((void)std::declval<G &>()(std::declval<Args>()...), std::true_type{});
template<typename, typename...> std::false_type callable_test(...);
template<typename G, typename... Args> struct is_callable_with : decltype(callable_test<G, Args...>(0)) {};

// Empty-callable test: only function pointers and std::function can be null.
template<typename T> bool is_null(const T &) {
  return false;
}
template<typename R, typename... A> bool is_null(R (*p)(A...)) {
  return p == nullptr;
}
template<typename S> bool is_null(const std::function<S> &f) {
  return !f;
}

template<int N> using tag = std::integral_constant<int, N>;
template<typename> struct always_false : std::false_type {};

// Adapters hold the user's functor directly, so there is one type erasure, at assignment.
template<typename F> struct SseWithRequest {
  F f;
  void operator()(AsyncWebServerRequest *r, const AsyncEventSourceClientPtr &c) {
    f(r, c);
  }
};
template<typename F> struct SseFromShared {
  F f;
  void operator()(AsyncWebServerRequest *, const AsyncEventSourceClientPtr &c) {
    f(c);
  }
  void operator()(const AsyncEventSourceClientPtr &c) {
    f(c);
  }
};
template<typename F> struct SseFromRaw {
  F f;
  void operator()(AsyncWebServerRequest *, const AsyncEventSourceClientPtr &c) {
    f(c.get());
  }
  void operator()(const AsyncEventSourceClientPtr &c) {
    f(c.get());
  }
};

// Richest form first, so a callable accepting several resolves deterministically.
// Each probe is instantiated only if the one before it failed: probing a generic
// lambda instantiates its body, and a body error there is a hard error.
template<typename F, bool = is_callable_with<F, AsyncEventSourceClient *>::value> struct sse_raw_kind : tag<1> {};
template<typename F> struct sse_raw_kind<F, false> : tag<0> {};
template<typename F, bool = is_callable_with<F, const AsyncEventSourceClientPtr &>::value> struct sse_disconnect_kind : tag<2> {};
template<typename F> struct sse_disconnect_kind<F, false> : sse_raw_kind<F> {};
template<typename F, bool = is_callable_with<F, AsyncWebServerRequest *, const AsyncEventSourceClientPtr &>::value> struct sse_connect_kind : tag<3> {};
template<typename F> struct sse_connect_kind<F, false> : sse_disconnect_kind<F> {};

template<typename F> ArEventConnectHandler make_sse_connect_handler(F &&cb, tag<3>) {
  return SseWithRequest<typename std::decay<F>::type>{std::forward<F>(cb)};
}
template<typename F> ArEventConnectHandler make_sse_connect_handler(F &&cb, tag<2>) {
  return SseFromShared<typename std::decay<F>::type>{std::forward<F>(cb)};
}
template<typename F> ArEventConnectHandler make_sse_connect_handler(F &&cb, tag<1>) {
  return SseFromRaw<typename std::decay<F>::type>{std::forward<F>(cb)};
}
template<typename F> ArEventConnectHandler make_sse_connect_handler(F &&, tag<0>) {
  static_assert(
    always_false<F>::value, "onConnect() callback must be callable as void(AsyncEventSourceClient *), "
                            "void(const AsyncEventSourceClientPtr &), or "
                            "void(AsyncWebServerRequest *, const AsyncEventSourceClientPtr &)"
  );
  return nullptr;
}

template<typename F> ArEventDisconnectHandler make_sse_disconnect_handler(F &&cb, tag<2>) {
  return SseFromShared<typename std::decay<F>::type>{std::forward<F>(cb)};
}
template<typename F> ArEventDisconnectHandler make_sse_disconnect_handler(F &&cb, tag<1>) {
  return SseFromRaw<typename std::decay<F>::type>{std::forward<F>(cb)};
}
template<typename F> ArEventDisconnectHandler make_sse_disconnect_handler(F &&, tag<0>) {
  static_assert(
    always_false<F>::value, "onDisconnect() callback must be callable as void(AsyncEventSourceClient *) "
                            "or void(const AsyncEventSourceClientPtr &)"
  );
  return nullptr;
}

}  // namespace detail
}  // namespace asyncsrv

template<typename F> void AsyncEventSource::onConnect(F &&cb) {
  if (asyncsrv::detail::is_null(cb)) {
    _connectcb = nullptr;
    return;
  }
  _connectcb =
    asyncsrv::detail::make_sse_connect_handler(std::forward<F>(cb), typename asyncsrv::detail::sse_connect_kind<typename std::decay<F>::type>::type{});
}

template<typename F> void AsyncEventSource::onDisconnect(F &&cb) {
  if (asyncsrv::detail::is_null(cb)) {
    _disconnectcb = nullptr;
    return;
  }
  _disconnectcb =
    asyncsrv::detail::make_sse_disconnect_handler(std::forward<F>(cb), typename asyncsrv::detail::sse_disconnect_kind<typename std::decay<F>::type>::type{});
}
