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

#include <list>
#include <memory>
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
using ArEventHandlerFunctionSafe = std::function<void(const AsyncEventSourceClientPtr &client)>;
using ArEventRequestConnectHandlerFunction = std::function<void(AsyncWebServerRequest *request, const AsyncEventSourceClientPtr &client)>;
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
  // detach from the owning server (used when the server dies before the client);
  // clears AsyncTCP callbacks and closes the connection
  void _detach();

public:
  /**
   * @brief Construct a new Async Event Source Client object
   * @note constructor is normally passed a client object from AsyncWebServerRequest::releaseClient(); see AsyncEventSourceResponse::_respond()
   * @note the client does not register itself with the server; ownership passes to
   *       AsyncEventSource::_addClient(), which stores it in the shared client list
   *
   * @param client
   * @param server
   * @param lastId
   */
  AsyncEventSourceClient(AsyncClient *client, AsyncEventSource *server, uint32_t lastId = 0);
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
    return connected() && _queueMessage(std::move(message));
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

  // system callbacks (do not call if from user code!)
  void _onAck(size_t len, uint32_t time);
  void _onPoll();
  void _onTimeout(uint32_t time);
  void _onDisconnect();
};

/**
 * @brief a class that maintains all connected HTTP clients subscribed to SSE delivery
 * dispatches supplied messages to the client's queues
 *
 */
class AsyncEventSource : public AsyncWebHandler {
private:
  String _url;
  std::list<AsyncEventSourceClientPtr> _clients;
  // Same as for individual messages, protect mutations of _clients list
  // since simultaneous access from different tasks is possible
  mutable asyncsrv::mutex_type _client_queue_lock;
  // One callback slot per event: the raw-pointer setters (onConnect/onDisconnect)
  // wrap their callback into the shared-handle form, so both flavors share storage
  ArEventHandlerFunctionSafe _connectsafecb = nullptr;
  ArEventHandlerFunctionSafe _disconnectsafecb = nullptr;
  ArEventRequestConnectHandlerFunction _requestConnectcb = nullptr;

  // this method manipulates in-fligh data size for connected client depending on number of active connections
  void _adjust_inflight_window();

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
     * @brief set on-connect callback for the client (raw-pointer flavor)
     * used to deliver messages to client on first connect
     * @note the pointer is valid only during the callback and must not be stored.
     *       Shares the slot with onConnectSafe(): the last registered callback wins.
     * @note prefer onConnectSafe(): it delivers a handle that may also be stored
     *
     * @param cb
     */
  void onConnect(ArEventHandlerFunction cb);

  /**
     * @brief set on-connect callback delivering an ownership-safe shared handle
     * The handle may be stored and used past the end of the callback (e.g. to
     * close this connection later or to send targeted messages); the client
     * object stays alive until the last handle is dropped.
     */
  void onConnectSafe(ArEventHandlerFunctionSafe cb) {
    _connectsafecb = cb;
  }

  /**
     * @brief set an on-connect callback that also receives the originating request
     * @note the request pointer is valid only for the duration of the callback
     *       (read authentication/session state here); the client handle may be stored
     */
  void onConnectWithRequest(ArEventRequestConnectHandlerFunction cb) {
    _requestConnectcb = cb;
  }

  // The client pointer sent to the callback is valid for the duration of the callback
  // (the server keeps the object alive while the callback runs); it must not be
  // stored. Prefer onDisconnectSafe(): it delivers a handle that may be stored.
  // Shares the slot with onDisconnectSafe(): the last registered callback wins.
  void onDisconnect(ArEventHandlerFunction cb);

  /**
     * @brief set on-disconnect callback delivering an ownership-safe shared handle
     * Safe to call methods on the handle inside the callback.
     */
  void onDisconnectSafe(ArEventHandlerFunctionSafe cb) {
    _disconnectsafecb = cb;
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

  // system callbacks (do not call from user code!)
  // registers a pre-allocated client; when a request is supplied, adopts its
  // AsyncClient (ownership transfer) before registration
  void _addClient(const AsyncEventSourceClientPtr &client, AsyncWebServerRequest *request = nullptr);
  void _handleDisconnect(AsyncEventSourceClient *client);
  bool canHandle(AsyncWebServerRequest *request) const final;
  void handleRequest(AsyncWebServerRequest *request) final;
};

class AsyncEventSourceResponse : public AsyncWebServerResponse {
private:
  AsyncEventSource *_server;

public:
  AsyncEventSourceResponse(AsyncEventSource *server);
  void _respond(AsyncWebServerRequest *request) override;
  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time) override {
    return 0;
  };
  bool _sourceValid() const override {
    return true;
  }
};
