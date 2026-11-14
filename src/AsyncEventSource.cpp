// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#include "AsyncEventSource.h"
#include "AsyncWebServerLogging.h"

#include <algorithm>
#include <list>
#include <memory>
#include <utility>
#include <vector>

#define ASYNC_SSE_NEW_LINE_CHAR (char)0xa

using namespace asyncsrv;

static String generateEventMessage(const char *message, const char *event, uint32_t id, uint32_t reconnect) {
  String str;
  size_t len{0};
  if (message) {
    len += strlen(message);
  }

  if (event) {
    len += strlen(event);
  }

  len += 42;  // give it some overhead

  if (!str.reserve(len)) {
    async_ws_log_e("Failed to allocate");
    return asyncsrv::emptyString;
  }

  if (reconnect) {
    str += T_retry_;
    str += reconnect;
    str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'
  }

  if (id) {
    str += T_id__;
    str += id;
    str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'
  }

  if (event != NULL) {
    str += T_event_;
    str += event;
    str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'
  }

  if (!message) {
    return str;
  }

  size_t messageLen = strlen(message);
  char *lineStart = (char *)message;
  char *lineEnd;
  do {
    char *nextN = strchr(lineStart, '\n');
    char *nextR = strchr(lineStart, '\r');
    if (nextN == NULL && nextR == NULL) {
      // a message is a single-line string
      str += T_data_;
      str += message;
      str += T_nn;
      return str;
    }

    // a message is a multi-line string
    char *nextLine = nullptr;
    if (nextN != NULL && nextR != NULL) {  // windows line-ending \r\n
      if (nextR + 1 == nextN) {
        // normal \r\n sequence
        lineEnd = nextR;
        nextLine = nextN + 1;
      } else {
        // some abnormal \n \r mixed sequence
        lineEnd = std::min(nextR, nextN);
        nextLine = lineEnd + 1;
      }
    } else if (nextN != NULL) {  // Unix/Mac OS X LF
      lineEnd = nextN;
      nextLine = nextN + 1;
    } else {  // some ancient garbage
      lineEnd = nextR;
      nextLine = nextR + 1;
    }

    str += T_data_;
    str.concat(lineStart, lineEnd - lineStart);
    str += ASYNC_SSE_NEW_LINE_CHAR;  // \n

    lineStart = nextLine;
  } while (lineStart < ((char *)message + messageLen));

  // append another \n to terminate message
  str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'

  return str;
}

// Client

AsyncEventSourceClient::AsyncEventSourceClient(AsyncClient *client, AsyncEventSource *server, uint32_t lastId)
  : _client(client), _server(server), _lastId(lastId) {}

void AsyncEventSourceClient::_attach() {
  _client->setRxTimeout(0);
  _client->onError(NULL, NULL);
  _client->onAck(
    [](void *r, AsyncClient *c, size_t len, uint32_t time) {
      (void)c;
      // Pin the client for the duration of the callback so a disconnect
      // inside AsyncTCP cannot free it while this frame still runs.
      AsyncEventSourceClientPtr self = static_cast<AsyncEventSourceClient *>(r)->shared_from_this();
      self->_onAck(len, time);
    },
    this
  );
  _client->onPoll(
    [](void *r, AsyncClient *c) {
      (void)c;
      AsyncEventSourceClientPtr self = static_cast<AsyncEventSourceClient *>(r)->shared_from_this();
      self->_onPoll();
    },
    this
  );
  _client->onData(NULL, NULL);
  _client->onTimeout(
    [](void *r, AsyncClient *c, uint32_t time) {
      (void)c;
      AsyncEventSourceClientPtr self = static_cast<AsyncEventSourceClient *>(r)->shared_from_this();
      self->_onTimeout(time);
    },
    this
  );
  _client->onDisconnect(
    [](void *r, AsyncClient *c) {
      // Pin first: _onDisconnect() may end up dropping the last server-side
      // reference; the client must survive until this frame returns, and
      // the AsyncClient delete must happen on this AsyncTCP callback frame.
      AsyncEventSourceClientPtr self = static_cast<AsyncEventSourceClient *>(r)->shared_from_this();
      self->_onDisconnect();
      delete c;
    },
    this
  );
  _client->setNoDelay(true);
}

AsyncEventSourceClient::~AsyncEventSourceClient() {
  // Protect message queue access (size checks and modifications) which is not thread-safe.
  {
    asyncsrv::lock_guard_type lock(_lockmq);
    _messageQueue.clear();
  }
  if (_client) {
    // Still connected: the last handle was dropped (or the server was torn
    // down) before the TCP connection ended. Close it directly, but detach the
    // AsyncTCP callbacks first -- shared_from_this() is invalid inside a
    // destructor, so no callback may re-enter this object while it dies.
    _client->onDisconnect({}, nullptr);
    _client->onAck({}, nullptr);
    _client->onPoll({}, nullptr);
    _client->onTimeout({}, nullptr);
    delete _client;  // ~AsyncClient closes the connection
    _client = nullptr;
  }
}

bool AsyncEventSourceClient::_queueMessage(AsyncEvent_SharedData_t &&msg) {
  // Protect message queue access (size checks and modifications) which is not thread-safe.
  asyncsrv::lock_guard_type lock(_lockmq);

  if (_messageQueue.size() >= SSE_MAX_QUEUED_MESSAGES) {
    async_ws_log_w("Event message queue overflow: discard message");
    return false;
  }

  if (!msg || msg->length() == 0) {
    return false;  // Invalid message
  }

  if (_client) {
    _messageQueue.emplace_back(std::move(msg));
  } else {
    _messageQueue.clear();
    return false;
  }

  // Send new content if we're not waiting on network buffer space
  if (!_ack_pending && (_inflight < _max_inflight)) {
    _runQueue();
  }
  return true;
}

void AsyncEventSourceClient::_onAck(size_t len, uint32_t time __attribute__((unused))) {
  // Protect message queue access (size checks and modifications) which is not thread-safe.
  asyncsrv::lock_guard_type lock(_lockmq);

  // adjust in-flight len
  if (len < _inflight) {
    _inflight -= len;
  } else {
    _inflight = 0;
  }

  _ack_pending = false;  // some space has been cleared

  // try to send another batch of data
  if (_messageQueue.size()) {
    _runQueue();
  }
}

void AsyncEventSourceClient::_onPoll() {
  // Protect message queue access (size checks and modifications) which is not thread-safe.
  asyncsrv::lock_guard_type lock(_lockmq);
  if ((_messageQueue.size()) && (_inflight < _max_inflight)) {
    _runQueue();
  }
}

void AsyncEventSourceClient::_onTimeout(uint32_t time __attribute__((unused))) {
  if (_client) {
    _client->close();
  }
}

void AsyncEventSourceClient::_onDisconnect() {
  if (!_client) {
    return;
  }
  {
    // Every queue method (_queueMessage, _runQueue, _onAck, _onPoll) reads
    // _client while holding _lockmq; for those guarded reads to be
    // meaningful, this write must be synchronized the same way.
    asyncsrv::lock_guard_type lock(_lockmq);
    _client = nullptr;
  }
  if (_server) {
    _server->_handleDisconnect(this);
  }
  // Drop queued messages: they can never be delivered anymore. Do it after
  // the disconnect callback so packetsWaiting() still reports the backlog
  // there, as it did when the client was destroyed immediately on erase.
  // Without this, an app holding a handle would keep the backlog in memory
  // until the last handle is dropped.
  {
    asyncsrv::lock_guard_type lock(_lockmq);
    _messageQueue.clear();
  }
}

void AsyncEventSourceClient::close() {
  if (_client) {
    _client->close();
  }
}

void AsyncEventSourceClient::_detach() {
  _server = nullptr;
  if (_client) {
    // Detach AsyncTCP callbacks first so the close cannot re-enter this
    // object after the owning server is gone.
    _client->onDisconnect({}, nullptr);
    _client->onAck({}, nullptr);
    _client->onPoll({}, nullptr);
    _client->onTimeout({}, nullptr);
    delete _client;  // ~AsyncClient closes the connection
    _client = nullptr;
  }
}

bool AsyncEventSourceClient::send(const char *message, const char *event, uint32_t id, uint32_t reconnect) {
  if (!connected()) {
    return false;
  }
  return _queueMessage(std::make_shared<String>(generateEventMessage(message, event, id, reconnect)));
}

void AsyncEventSourceClient::_runQueue() {
  if (!_client) {
    return;
  }

  // there is no need to lock the mutex here, 'cause all the calls to this method must be already lock'ed
  size_t total_bytes_written = 0;
  while (!_messageQueue.empty()) {
    auto &data = _messageQueue.front();
    size_t len = data->length() - _sent;
    /*
      add() would call lwip's tcp_write() under the AsyncTCP hood with apiflags argument.
      By default apiflags=ASYNC_WRITE_FLAG_COPY
      we could have used apiflags with this flag unset to pass data by reference and avoid copy to socket buffer,
      but looks like it does not work for Arduino's lwip in ESP32/IDF
      it is enforced in https://github.com/espressif/esp-lwip/blob/0606eed9d8b98a797514fdf6eabb4daf1c8c8cd9/src/core/tcp_out.c#L422C5-L422C30
      if LWIP_NETIF_TX_SINGLE_PBUF is set, and it is set indeed in IDF
      https://github.com/espressif/esp-idf/blob/a0f798cfc4bbd624aab52b2c194d219e242d80c1/components/lwip/port/include/lwipopts.h#L744

      So let's just keep it enforced ASYNC_WRITE_FLAG_COPY and keep in mind that there is no zero-copy
    */
    size_t bytes_written = _client->add(data->c_str() + _sent, len, ASYNC_WRITE_FLAG_COPY);  //  ASYNC_WRITE_FLAG_MORE
    if (bytes_written == 0) {
      break;  // Send buffer is 100% full
    }
    total_bytes_written += bytes_written;
    _inflight += bytes_written;
    if ((_sent + bytes_written) == data->length()) {
      _messageQueue.pop_front();
      _sent = 0;
    } else {
      _sent += bytes_written;
    }
    if (_sent || _inflight > _max_inflight) {
      // Incomplete message or inflight bytes exceed the maximum allowed
      break;
    }
  }

  // If we exited the loop with messages still in the queue, it means we couldn't send all of them;
  // flag that we're waiting for an acknowledgment from the client
  if (!_messageQueue.empty()) {
    _ack_pending = true;
  }

  // flush socket
  if (_client && total_bytes_written) {
    _client->send();
  }
}

void AsyncEventSourceClient::set_max_inflight_bytes(size_t value) {
  if (value >= SSE_MIN_INFLIGH && value <= SSE_MAX_INFLIGH) {
    _max_inflight = value;
  }
}

/*  AsyncEventSource  */

AsyncEventSource::~AsyncEventSource() {
  close();
  // Detach any client whose handle an application may still hold: the server
  // object is going away, so clients must not touch it anymore. New clients
  // cannot appear past this point: the handler is removed from the server
  // before destruction begins.
  std::list<AsyncEventSourceClientPtr> leftovers;
  {
    asyncsrv::lock_guard_type lock(_client_queue_lock);
    leftovers.splice(leftovers.end(), _clients);
  }
  for (const auto &c : leftovers) {
    c->_detach();
  }
}

// wrap the raw-pointer flavor into the shared-handle slot; std::function owns
// its callable, so an anonymous temporary registered by the user stays alive
void AsyncEventSource::onConnect(ArEventHandlerFunction cb) {
  if (cb) {
    _connectsafecb = [cb](const AsyncEventSourceClientPtr &client) {
      cb(client.get());
    };
  } else {
    _connectsafecb = nullptr;
  }
}

void AsyncEventSource::onDisconnect(ArEventHandlerFunction cb) {
  if (cb) {
    _disconnectsafecb = [cb](const AsyncEventSourceClientPtr &client) {
      cb(client.get());
    };
  } else {
    _disconnectsafecb = nullptr;
  }
}

void AsyncEventSource::authorizeConnect(ArAuthorizeConnectHandler cb) {
  AsyncAuthorizationMiddleware *m = new AsyncAuthorizationMiddleware(401, cb);
  m->_freeOnRemoval = true;
  addMiddleware(m);
}

void AsyncEventSource::_addClient(const AsyncEventSourceClientPtr &client, AsyncWebServerRequest *request) {
  if (!client) {
    return;
  }
  if (!client->_client && request) {
    // Adopt the connection from the request. The client was pre-allocated
    // (before any bytes were written to the socket) in AsyncEventSourceResponse::_respond().
    client->_client = request->clientRelease();
  }
  if (!client->_client) {
    // No connection to adopt (e.g. the request was already gone): drop the
    // half-constructed client instead of registering a zombie.
    return;
  }

  // Arm the AsyncTCP callbacks under the list lock, together with the list
  // insertion: the disconnect callback is armed by _attach() itself
  // (clientRelease() cleared them beforehand), so a disconnect racing this
  // section can only run after we release the lock -- and _handleDisconnect()
  // then finds the client in the list and erases it cleanly. Attaching and
  // inserting in separate critical sections would let a disconnect run in
  // between and erase from an empty list, leaving a dead entry behind.
  // Callbacks are attached here (not in the constructor) because they rely on
  // shared_from_this(), which requires shared_ptr ownership.
  {
    asyncsrv::lock_guard_type lock(_client_queue_lock);
    client->_attach();
    _clients.emplace_back(client);
    _adjust_inflight_window();
  }

  // Fire user callbacks outside the list lock, after registration: a
  // broadcast or close inside a connect callback now behaves consistently
  // (the client is part of the list while its callback runs).
  if (_connectsafecb) {
    _connectsafecb(client);
  }
  if (_requestConnectcb) {
    _requestConnectcb(request, client);
  }
}

void AsyncEventSource::_handleDisconnect(AsyncEventSourceClient *client) {
  // The client is kept alive by the caller's shared handle (network callback
  // pin or app-held handle), so firing callbacks with a usable object is safe.
  if (_disconnectsafecb) {
    _disconnectsafecb(client->shared_from_this());
  }
  asyncsrv::lock_guard_type lock(_client_queue_lock);
  for (auto i = _clients.begin(); i != _clients.end(); ++i) {
    if (i->get() == client) {
      _clients.erase(i);
      break;
    }
  }
  _adjust_inflight_window();
}

void AsyncEventSource::close() {
  // Snapshot the list under the lock, close outside it: a synchronous
  // _onDisconnect() inside c->close() erases list nodes, which would
  // otherwise invalidate the loop iterator (and could deadlock).
  std::vector<AsyncEventSourceClientPtr> live;
  {
    asyncsrv::lock_guard_type lock(_client_queue_lock);
    for (const auto &c : _clients) {
      live.push_back(c);
    }
  }
  for (const auto &c : live) {
    if (c->connected()) {
      c->close();
    }
  }
}

std::vector<AsyncEventSourceClientPtr> AsyncEventSource::getClients() const {
  std::vector<AsyncEventSourceClientPtr> snapshot;
  asyncsrv::lock_guard_type lock(_client_queue_lock);
  snapshot.reserve(_clients.size());
  for (const auto &c : _clients) {
    snapshot.push_back(c);
  }
  return snapshot;
}

// pmb fix
size_t AsyncEventSource::avgPacketsWaiting() const {
  size_t aql = 0;
  uint32_t nConnectedClients = 0;
  asyncsrv::lock_guard_type lock(_client_queue_lock);
  for (const auto &c : _clients) {
    if (c->connected()) {
      aql += c->packetsWaiting();
      ++nConnectedClients;
    }
  }
  return nConnectedClients == 0 ? 0 : ((aql) + (nConnectedClients / 2)) / (nConnectedClients);  // round up
}

AsyncEventSource::SendStatus AsyncEventSource::send(const char *message, const char *event, uint32_t id, uint32_t reconnect) {
  AsyncEvent_SharedData_t shared_msg = std::make_shared<String>(generateEventMessage(message, event, id, reconnect));
  asyncsrv::lock_guard_type lock(_client_queue_lock);
  size_t hits = 0;
  size_t miss = 0;
  for (const auto &c : _clients) {
    if (c->connected()) {
      if (c->write(shared_msg)) {
        ++hits;
      } else {
        ++miss;
      }
    }
  }
  return hits == 0 ? DISCARDED : (miss == 0 ? ENQUEUED : PARTIALLY_ENQUEUED);
}

size_t AsyncEventSource::count() const {
  asyncsrv::lock_guard_type lock(_client_queue_lock);
  size_t n_clients{0};
  for (const auto &i : _clients) {
    if (i->connected()) {
      ++n_clients;
    }
  }

  return n_clients;
}

bool AsyncEventSource::canHandle(AsyncWebServerRequest *request) const {
  return request->isSSE() && request->url().equals(_url);
}

void AsyncEventSource::handleRequest(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = new (std::nothrow) AsyncEventSourceResponse(this);
  if (!response) {
    async_ws_log_e("Failed to allocate");
    request->sendRawResponse(asyncsrv::T_HTTP_503_HEAD);
    return;
  }
  request->send(response);
}

// list iteration protected by caller's lock
void AsyncEventSource::_adjust_inflight_window() {
  const size_t clientCount = count();
  if (clientCount) {
    size_t inflight = SSE_MAX_INFLIGH / clientCount;
    for (const auto &c : _clients) {
      if (c->connected()) {
        c->set_max_inflight_bytes(inflight);
      }
    }
    // Serial.printf("adjusted inflight to: %u\n", inflight);
  }
}

/*  Response  */

AsyncEventSourceResponse::AsyncEventSourceResponse(AsyncEventSource *server) : _server(server) {
  _code = 200;
  _contentType = T_text_event_stream;
  _sendContentLength = false;
  addHeader(T_Cache_Control, T_no_cache);
  addHeader(T_Connection, T_keep_alive);
}

void AsyncEventSourceResponse::_respond(AsyncWebServerRequest *request) {
  String out;
  if (!_assembleHead(out, request->version())) {
    // Head assembly ran out of memory: fail the stream instead of sending a
    // corrupted head.
    async_ws_log_e("Failed to allocate");
    _state = RESPONSE_FAILED;
    request->sendRawResponse(asyncsrv::T_HTTP_503_HEAD);
    return;
  }
  uint32_t lastId = 0;
  if (request->hasHeader(T_Last_Event_ID)) {
    lastId = strtoul(request->getHeader(T_Last_Event_ID)->value().c_str(), nullptr, 10);
  }
  // Allocate the SSE client before committing anything to the connection, so
  // an allocation failure can still be answered with 503 on the raw HTTP
  // connection (after clientRelease() the request can no longer respond).
  // The connection itself is adopted from the request inside _addClient().
  AsyncEventSourceClientPtr client(new (std::nothrow) AsyncEventSourceClient(nullptr, _server, lastId));
  if (!client) {
    async_ws_log_e("Failed to allocate");
    _state = RESPONSE_FAILED;
    request->sendRawResponse(asyncsrv::T_HTTP_503_HEAD);
    return;
  }
  request->client()->write(out.c_str(), _headLength, ASYNC_WRITE_FLAG_COPY);
  // Hold the request in scope for the connect callback to inspect
  // (clientRelease() inside _addClient() drops the request's self-reference)
  std::shared_ptr<AsyncWebServerRequest> reqLock = request->shared_from_this();
  _server->_addClient(client, request);
}
