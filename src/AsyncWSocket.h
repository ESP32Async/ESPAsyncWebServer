// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

// A new experimental implementation of Async WebSockets client/server


#pragma once

#include "AsyncWebSocket.h"
#include "freertos/FreeRTOS.h"

#ifndef WS_IN_FLIGHT_CREDITS
#define WS_IN_FLIGHT_CREDITS  4
#endif

// forward declaration for WSocketServer
class WSocketServer;

/**
 * @brief WebSocket message type
 * 
 */
enum class WSFrameType_t : uint8_t {   // type field is 4 bits
  continuation = 0,        // fragment of a previous message
  text,
  binary,
  close = 0x08,
  ping,
  pong
};

/**
 * @brief Message transition state
 * reflects message transmission state
 * 
 */
enum class WSMessageStatus_t {
  empty = 0,      // bare message, not usefull for anything yet
  sending,        // message in sending state
  sent,           // message transition has been complete
  incomlete,      // message is not complete. i.e. partially received
  complete,       // message is complete, no new data is expected
  error           // malformed message
};



/**
 * @brief abstract WebSocket message
 * 
 */
class WSMessageGeneric {
  friend class WSocketClient;
  /** Is this the last chunk in a fragmented message ?*/
  bool _final;

public:
  const WSFrameType_t type;

  WSMessageGeneric(WSFrameType_t type, bool final = true) : type(type), _final(final) {};
  virtual ~WSMessageGeneric(){};

  // if this message in final fragment
  bool final() const { return _final; }

  /**
   * @brief Get the Size of the message
   * 
   * @return size_t 
   */
  virtual size_t getSize() const = 0;

  /**
   * @brief Get access to RO-data
   * @note we cast it to char* 'cause of two reasons:
   *  - the LWIP's _tcp_write() function accepts const char*
   *  - websocket is mostly text anyway unless compressed
   * @note for messages with type 'text' this lib will always include NULL terminator at the end of data, NULL byte is NOT included in total size of the message returned by getSize()
   *        a care should be taken when accessing the data for messages with type 'binary', it won't have NUL terminator at the end and would be valid up to getSize() in length!
   * @note buffer should be writtable so that client class could apply masking on data if needed
   * @return char* const 
   */
  virtual char* getData() = 0;

  /**
   * @brief WebSocket message status code as defined in https://datatracker.ietf.org/doc/html/rfc6455#section-7.4
   * it could be used as integrity control or derived class features
   * 
   * @return uint16_t 0 - default code for correct message
   */
  virtual uint16_t getStatusCode() const { return 0; }

protected:

  /**
   * @brief access message buffer in chunks
   * this method is used internally by WSocketClient class when sending message. In simple case it just wraps around getDtata() call
   * but could also be implemented in derived classes for chunked transfers
   * 
   * @return std::pair<char*, int32_t>
   */
  virtual std::pair<char*, size_t> getCurrentChunk(){ return std::pair(getData(), getSize()); } ;

  /**
   * @brief move to the next chunk of message data assuming current chunk has been consumed already
   * @note nextChunk call switches the pointer, getCurrentChunk() would return new chunk afterwards
   * 
   * @return int32_t - size of a next chunk of data
   * @note if returned size is '-1' then no more data chunks are available
   * @note if returned size is '0' then next chunk is not available yet, a call should be repeated later to retreive new chunk
   */
  virtual int32_t getNextChunk(){ return -1; };

  /**
   * @brief add a chunk of data to the message
   * it will NOT extend total message size, it's to reassemble a message taking several TCP packets
   * 
   * @param data - data chunk pointer
   * @param len - size of a chunk
   * @param offset - an offset for chunk from begingn of message (@note offset is for calculation reference only, the chunk is always added to the end of current message data)
   */
  virtual void addChunk(char* data, size_t len, size_t offset) = 0;

};

using WSMessagePtr = std::shared_ptr<WSMessageGeneric>;

template<typename T>
class WSMessageContainer : public WSMessageGeneric {
protected:
  T container;

public:

  WSMessageContainer(WSFrameType_t t, bool final = true) :  WSMessageGeneric(t, final) {}

  // variadic constructor for anything that T can be made of
  template<typename... Args>
  WSMessageContainer (WSFrameType_t t, bool final, Args&&... args) : WSMessageGeneric(t, final), container(std::forward<Args>(args)...) {};

  /**
   * @copydoc WSMessageGeneric::getSize()
   */
  size_t getSize() const override {
    // specialisation for Arduino String
    if constexpr(std::is_same_v<String, std::decay_t<decltype(container)>>)
      return container.length();
    // otherwise we assume either STL container is used, (i.e. st::vector or std::string) or derived class should implement same methods
    if constexpr(!std::is_same_v<String, std::decay_t<decltype(container)>>)
      return container.size();
  };

  /**
   * @copydoc WSMessageGeneric::getData()
   * @details though casted to const char* the data there is NOT NULL-terminated string!
   */
  char* getData() override {
    // specialization for Arduino String
    if constexpr(std::is_same_v<String, std::decay_t<decltype(container)>>)
      return container.c_str();
    // otherwise we assume either STL container is used, (i.e. st::vector or std::string) or derived class should implement same methods
    if constexpr(!std::is_same_v<String, std::decay_t<decltype(container)>>)
      return reinterpret_cast<char*>(container.data());
  }

  // access message container object
  T& getContainer(){ return container; }

protected:

  /**
   * @copydoc WSMessageGeneric::addChunk(char* data, size_t len, size_t offset)
   * @details though casted to const char* the data there is NOT NULL-terminated string!
   */
  void addChunk(char* data, size_t len, size_t offset) override {
    // specialization for Arduino String
    if constexpr(std::is_same_v<String, std::decay_t<decltype(container)>>){
      container.concat(data, len);
    }

    // specialization for std::string
    if constexpr(std::is_same_v<std::string, std::decay_t<decltype(container)>>){
      container.append(data, len);
    }

    // specialization for std::vector
    if constexpr(std::is_same_v<std::vector<uint8_t>, std::decay_t<decltype(container)>>){
      container.resize(len + offset);
      memcpy(container.data(), data, len);
    }
  }

};

/**
 * @brief Control message - 'Close'
 * 
 */
class WSMessageClose : public WSMessageContainer<std::string> {
  const uint16_t _status_code;

public:
  /**
   * @brief Construct Close message without the body
   * 
   * @param status close code as defined in https://datatracker.ietf.org/doc/html/rfc6455#section-7.4
   */
  WSMessageClose (uint16_t status = 1000);
  // variadic constructor for anything that std::string can be made of
  template<typename... Args>
  WSMessageClose (uint16_t status, Args&&... args) : WSMessageContainer<std::string>(WSFrameType_t::close, true, std::forward<Args>(args)...), _status_code(status) {
    // convert code to message body
    uint16_t buff = htons (status);
    container.append((char*)(&buff), 2);
  };
  
  uint16_t getStatusCode() const override { return _status_code; }
};

/**
 * @brief Dummy message that does not carry any data
 * could be used as a container for bodyless control messages or 
 * specific cased (to gracefully handle oversised incoming messages)
 */
class WSMessageDummy : public WSMessageGeneric {
  const uint16_t _code;
public:
  explicit WSMessageDummy(WSFrameType_t type, uint16_t status_code = 0) : WSMessageGeneric(type, true), _code(status_code) {};
  size_t getSize() const override { return 0; };
  char* getData() override { return nullptr; };
  uint16_t getStatusCode() const override { return _code; }

protected:
  void addChunk(char* data, size_t len, size_t offset) override {};
};


/**
 * @brief structure that owns the message (or fragment) while sending/receiving by WSocketClient
 */
struct WSMessageFrame {
  /** Mask key */
  uint32_t mask;
  /** Length of the current message/fragment to be transmitted. This equals the total length of the message if num == 0 && final == true */
  uint64_t len;
  /** Offset of the payload data pending to be sent. Note: this is NOT websocket message fragment's size! */
  uint64_t index;
  /** offset in the current chunk of data, used to when sending message in chunks (not WS fragments!), for single chunged messages chunk_offset and index are same */
  size_t chunk_offset;
  // message object
  WSMessagePtr msg;
};

/**
 * @brief WebSocket client instance
 * 
 */
class WSocketClient {
public:
  // TCP connection state
  enum class conn_state_t {
    connected,      // connected and exchangin messages
    disconnecting,  // awaiting close ack
    disconnected    // ws client is disconnected
  };

  /**
   * @brief WebSocket Client Events 
   * 
   */
  enum class event_t {
    connect = 0x01,
    disconnect = 0x02,
    msgRecv = 0x04,
    msgSent = 0x08,
    msgDropped = 0x10,
    inQfull = 0x20
  };

  // error codes
  enum class err_t {
    ok,                 // no problem :)
    nospace,            // message queues are overflowed, can't accept more data
    messageTooBig,      // can't accept such a large message
    disconnected        // peer connection is broken
  };

  enum class overflow_t {
    disconnect,
    discard,
    drophead,
    droptail
  };

  // event callback alias
  using event_cb_t = std::function<void(WSocketClient *client, WSocketClient::event_t event)>;

  // Client connection ID (increments for each new connection for the given server)
  const uint32_t id;

private:
  AsyncClient *_client;
  event_cb_t _cb;
  // incoming message size limit
  size_t _max_msgsize;
  // cummulative maximum of the data messages held in message queues, both in and out
  size_t _max_qcap;

public:

  /**
   * @brief Construct a new WSocketClient object
   * 
   * @param id - client's id tag
   * @param request - AsyncWebServerRequest which is switching the protocol to WS
   * @param call_back - event callback handler
   * @param msgcap - incoming message size limit, if incoming msg advertizes larger size the connection would be dropped
   * @param qcap - in/out queues sizes (in number of messages)
   */
  WSocketClient(uint32_t id, AsyncWebServerRequest *request, WSocketClient::event_cb_t call_back, size_t msgsize = 8 * 1024, size_t qcap = 4);
  ~WSocketClient();

  /**
   * @brief Enqueue message for sending
   * 
   * @param msg rvalue reference to message object, i.e. WSocketClient will take the ownership of the object
   * @return err_t enqueue error status
   */
  err_t enqueueMessage(std::shared_ptr<WSMessageGeneric> mptr);

  /**
   * @brief retrieve message from inbout Q
   * 
   * @return WSMessagePtr
   * @note if Q is empty, then empty pointer is returned, so it should be validated
   */
  WSMessagePtr dequeueMessage();

  conn_state_t status() const {
    return _connection;
  }

  AsyncClient *client() {
    return _client;
  }

  const AsyncClient *client() const {
    return _client;
  }

  /**
   * @brief Set inbound queue overflow Policy
   * 
   * @param policy inbound queue overflow policy
   * 
   * @note overflow_t::disconnect (default) - disconnect client with respective close message when inbound Q is full.
   * This is the default behavior in yubox-node-org, which is not silently discarding messages but instead closes the connection.
   * The big issue with this behavior is  that is can cause the UI to automatically re-create a new WS connection, which can be filled again,
   * and so on, causing a resource exhaustion.
   * 
   * @note overflow_t::discard - silently discard new messages if the queue is full (only a discard event would be generated to notify about drop)
   * This is the default behavior in the original ESPAsyncWebServer library from me-no-dev. This behavior allows the best performance at the expense of unreliable message delivery in case the queue is full.
   * 
   * @note overflow_t::drophead - drop the oldest message from inbound queue to fit new message
   * @note overflow_t::droptail - drop most recent message from inbound queue to fit new message
   * 
   */
  void setOverflowPolicy(overflow_t policy){ _overflow_policy = policy; }

  overflow_t getOverflowPolicy() const { return _overflow_policy; }

  // send control frames
  err_t close(uint16_t code = 0, const char *message = NULL);
  err_t ping(const char *data = NULL, size_t len = 0);

  size_t inQueueSize() const { return _messageQueueIn.size(); };
  size_t outQueueSize() const { return _messageQueueOut.size(); };

  // check if client can enqueue and send new messages
  err_t canSend() const;

  /**
   * @brief access Event Group Handle for the client
   * 
   * @return EventGroupHandle_t 
   */
  EventGroupHandle_t getEventGroupHandle(){ return _eventGroup; };

  /**
   * @brief Create a Event Group for the client
   * if Event Group is created then client will set event bits in the group
   * when various events are generate, i.e. message received, connect/disconnect, etc...
   * 
   * @return EventGroupHandle_t 
   */
  EventGroupHandle_t createEventGroupHandle(){
    if (!_eventGroup) _eventGroup = xEventGroupCreate();
    return _eventGroup;
  }

private:
  conn_state_t _connection{conn_state_t::connected};
  // frames in transit
  WSMessageFrame _inFrame{}, _outFrame{};
  // message queues
  std::deque< WSMessagePtr > _messageQueueIn;
  std::deque< WSMessagePtr > _messageQueueOut;
  EventGroupHandle_t _eventGroup{nullptr};

#ifdef ESP32
  // access mutex'es
  std::mutex _sendLock;
  mutable std::recursive_mutex _inQlock;
  mutable std::recursive_mutex _outQlock;
#endif

  // inbound Q overflow behavior
  overflow_t _overflow_policy{overflow_t::disconnect};

  // amount of sent data in-flight, i.e. copied to socket buffer, but not acked yet from lwip side
  size_t _in_flight{0};
  // in-flight data credits
  size_t _in_flight_credit{WS_IN_FLIGHT_CREDITS};

  /**
   * @brief go through out Q and send message data ()
   * @note this method will grab a mutex lock on outQ internally
   * 
   */
  void _clientSend(size_t acked_bytes = 0);

  /**
   * @brief expell next message from out Q (if any) and start sending it to the peer
   * @note this function will generate and add msg header to socket buffer but assumes it's callee will take care of actually sending the header and message body further
   * @note this function assumes that it's calle has already set _outFrameLock
   * 
   */
  bool _evictOutQueue();

  /**
   * @brief try to parse ws header and create a new frame message
   * 
   * @return size_t size of the parsed and consumed bytes from input in a new message
   */
  std::pair<size_t, uint16_t> _mkNewFrame(char* data, size_t len, WSMessageFrame& frame);

  /**
   * @brief run a callback for event / set event group bits
   * 
   * @param e 
   */
  void _sendEvent(event_t e);

  // AsyncTCP callbacks
  void _onTimeout(uint32_t time);
  void _onDisconnect(AsyncClient *c);
  void _onData(void *pbuf, size_t plen);
};

/**
 * @brief WebServer Handler implementation that plays the role of a WebSocket server
 * it inherits behavior of original WebSocket server and uses AsyncTCP callback
 * to handle incoming messages
 * 
 */
class WSocketServer : public AsyncWebHandler {
public:
  // error enqueue to all
  enum class msgall_err_t {
    ok = 0,         // message was enqueued for delivering to all clients
    partial,        // some of clients queueus are full, message was not enqueued there
    none            // no clients or all outbound queues are full, message discarded
  };

  explicit WSocketServer(const char* url, WSocketClient::event_cb_t handler = {}) : _url(url), eventHandler(handler) {}
  ~WSocketServer() = default;

  /**
   * @brief check if client with specified id can accept new message for sending
   * 
   * @param id 
   * @return WSocketClient::err_t - ready to send only if returned value is err_t::ok, otherwise err reason is returned 
   */
  WSocketClient::err_t canSend(uint32_t id) const { return _getClient(id) ? _getClient(id)->canSend() : WSocketClient::err_t::disconnected; };

  /**
   * @brief check how many clients are available for sending data
   * 
   * @return msgall_err_t 
   */
  msgall_err_t canSend() const;

  // return number of active clients
  size_t activeClientsCount() const;

  // find if there is a client with specified id
  bool hasClient(uint32_t id) const {
    return _getClient(id) != nullptr;
  }

  /**
   * @copydoc WSClient::setOverflowPolicy(overflow_t policy)
   */
  void setOverflowPolicy(WSocketClient::overflow_t policy){ _overflow_policy = policy; }
  WSocketClient::overflow_t getOverflowPolicy() const { return _overflow_policy; }

  /**
   * @brief disconnect client
   * 
   * @param id 
   * @param code 
   * @param message 
   */
  void close(uint32_t id, uint16_t code = 0, const char *message = NULL){
    if (WSocketClient *c = _getClient(id)) c->close(code, message);
  }

  /**
   * @brief disconnect all clients
   * 
   * @param code 
   * @param message 
   */
  void closeAll(uint16_t code = 0, const char *message = NULL){ for (auto &c : _clients) { c.close(code, message); } }

  /**
   * @brief sned ping to client
   * 
   * @param id 
   * @param data 
   * @param len 
   * @return true 
   * @return false 
   */
  WSocketClient::err_t ping(uint32_t id, const char *data = NULL, size_t len = 0){
    if (WSocketClient *c = _getClient(id))
      return c->ping(data, len);
    else
      return WSocketClient::err_t::disconnected;
  }

  /**
   * @brief send ping to all clients
   * 
   * @param data 
   * @param len 
   * @return msgall_err_t 
   */
  msgall_err_t pingAll(const char *data = NULL, size_t len = 0);

  /**
   * @brief send message to specific client
   * 
   * @param id 
   * @param m 
   * @return WSocketClient::err_t 
   */
  WSocketClient::err_t message(uint32_t id, WSMessagePtr m){
    if (WSocketClient *c = _getClient(id))
      return c->enqueueMessage(std::move(m));
    else
      return WSocketClient::err_t::disconnected;
  }

  /**
   * @brief send message to all available clients
   * 
   * @param m 
   * @return msgall_err_t 
   */
  msgall_err_t messageAll(WSMessagePtr m);


  /*
  bool text(uint32_t id, const uint8_t *message, size_t len);
  bool text(uint32_t id, const char *message, size_t len);
  bool text(uint32_t id, const char *message);
  bool text(uint32_t id, const String &message);
  bool text(uint32_t id, AsyncWebSocketMessageBuffer *buffer);
  bool text(uint32_t id, AsyncWebSocketSharedBuffer buffer);

  enqueue_err_t textAll(const uint8_t *message, size_t len);
  enqueue_err_t textAll(const char *message, size_t len);
  enqueue_err_t textAll(const char *message);
  enqueue_err_t textAll(const String &message);
  enqueue_err_t textAll(AsyncWebSocketMessageBuffer *buffer);
  enqueue_err_t textAll(AsyncWebSocketSharedBuffer buffer);

  bool binary(uint32_t id, const uint8_t *message, size_t len);
  bool binary(uint32_t id, const char *message, size_t len);
  bool binary(uint32_t id, const char *message);
  bool binary(uint32_t id, const String &message);
  bool binary(uint32_t id, AsyncWebSocketMessageBuffer *buffer);
  bool binary(uint32_t id, AsyncWebSocketSharedBuffer buffer);

  enqueue_err_t binaryAll(const uint8_t *message, size_t len);
  enqueue_err_t binaryAll(const char *message, size_t len);
  enqueue_err_t binaryAll(const char *message);
  enqueue_err_t binaryAll(const String &message);
  enqueue_err_t binaryAll(AsyncWebSocketMessageBuffer *buffer);
  enqueue_err_t binaryAll(AsyncWebSocketSharedBuffer buffer);

  size_t printf(uint32_t id, const char *format, ...) __attribute__((format(printf, 3, 4)));
  size_t printfAll(const char *format, ...) __attribute__((format(printf, 2, 3)));
*/

  void handleHandshake(AwsHandshakeHandler handler) {
    _handshakeHandler = handler;
  }

  // return bound URL
  const char *url() const {
    return _url.c_str();
  }

  /**
   * @brief callback for AsyncServer - onboard new ws client
   * 
   * @param request 
   * @return true 
   * @return false 
   */
  virtual bool newClient(AsyncWebServerRequest *request);

protected:
  std::list<WSocketClient> _clients;
  #ifdef ESP32
  std::mutex clientslock;
  #endif
  // WSocketClient events handler
  WSocketClient::event_cb_t eventHandler;

  // return next available client's ID
  uint32_t getNextId() {
    return ++_cNextId;
  }

    /**
   * @brief go through clients list and remove those ones that are disconnected and have no messages pending
   * 
   */
  void _purgeClients();

private:
  std::string _url;
  AwsHandshakeHandler _handshakeHandler;
  uint32_t _cNextId{0};
  WSocketClient::overflow_t _overflow_policy{WSocketClient::overflow_t::disconnect};

  /**
   * @brief Get ptr to client with specified id
   * 
   * @param id 
   * @return WSocketClient* - nullptr if client not founc
   */
  WSocketClient* _getClient(uint32_t id);
  WSocketClient const* _getClient(uint32_t id) const;


  // WebServer methods
  bool canHandle(AsyncWebServerRequest *request) const override final { return request->isWebSocketUpgrade() && request->url().equals(_url.c_str()); };
  void handleRequest(AsyncWebServerRequest *request) override final;
};

/**
 * @brief WebServer Handler implementation that plays the role of a WebSocket server
 * 
 */
class WSocketServerWorker : public WSocketServer {
public:

  // event callback alias
  using event_cb_t = std::function<void(WSocketClient::event_t event, uint32_t client_id)>;
  // message callback alias
  using msg_cb_t = std::function<void(WSMessagePtr msg, uint32_t client_id)>;

  explicit WSocketServerWorker(const char* url, msg_cb_t msg_handler, event_cb_t event_handler)
    : WSocketServer(url), _mcb(msg_handler), _ecb(event_handler) {}

  ~WSocketServerWorker(){ stop(); };

  /**
   * @brief start worker task to process WS Messages
   * 
   * @param stack 
   * @param uxPriority 
   * @param xCoreID 
   */
  void start(uint32_t stack = 4096, UBaseType_t uxPriority = 4, BaseType_t xCoreID = tskNO_AFFINITY);

  /**
   * @brief stop worker task
   * 
   */
  void stop();

  /**
   * @brief Set Message Callback function
   * 
   * @param handler 
   */
  void setMessageHandler(msg_cb_t handler){ _mcb = handler; };

  /**
   * @brief Set Event Callback function
   * 
   * @param handler 
   */
  void setEventHandler(event_cb_t handler){ _ecb = handler; };

  /**
   * @brief callback for AsyncServer - onboard new ws client
   * 
   * @param request 
   * @return true 
   * @return false 
   */
  bool newClient(AsyncWebServerRequest *request) override;


private:
  msg_cb_t _mcb;
  event_cb_t _ecb;
  // worker task that handles messages
  TaskHandle_t    _task_hndlr{nullptr};
  void _taskRunner();

};
