// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

// A new experimental implementation of Async WebSockets client/server

#pragma once
#if __cplusplus >= 201703L
#ifndef ESP32
#warning "WSocket is now supported on ESP32 only"
#else


#include "AsyncWebSocket.h"
#include "freertos/FreeRTOS.h"

namespace asyncsrv {
// literals hashing
// https://learnmoderncpp.com/2020/06/01/strings-as-switch-case-labels/

inline constexpr auto hash_djb2a(const std::string_view sv) {
    uint32_t hash{ 5381 };
    for (unsigned char c : sv) {
      hash = ((hash << 5) + hash) ^ c;
    }
    return hash;
}

}

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
   * @note for messages with type 'text' this lib will ALWAYS include NULL terminator at the end of data, NULL byte is NOT included in total size of the message returned by getSize()
   *        a care should be taken when accessing the data for messages with type 'binary', it won't have NUL terminator at the end and would be valid up to getSize() in length!
   * @note buffer should be writtable so that client class could apply masking on data if needed
   * @return char* const 
   */
  virtual const char* getData() = 0;

  /**
   * @brief Get mutable access to underlying Buffer memory
   * container might not allow this and return nullptr in this case
   * 
   * @return char* 
   */
  virtual char* getBuffer(){ return nullptr; }

  /**
   * @brief WebSocket message status code as defined in https://datatracker.ietf.org/doc/html/rfc6455#section-7.4
   * it could be used as integrity control or derived class features
   * 
   * @return uint16_t 0 - default code for correct message
   */
  virtual uint16_t getStatusCode() const { return 0; }

protected:
  /** Is this the last chunk in a fragmented message ?*/
  bool _final;

  /**
   * @brief access message buffer in chunks
   * this method is used internally by WSocketClient class when sending message. In simple case it just wraps around getDtataConst() call
   * but could also be implemented in derived classes for chunked transfers
   * 
   * @return std::pair<char*, int32_t>
   */
  virtual std::pair<const char*, size_t> getCurrentChunk(){ return std::pair(getData(), getSize()); }

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

/**
 * @brief templated Message container
 * it can use any implementation of container classes to hold message data
 * two main types to use are std::string for text messages and std::vector for binary
 * @note Arduino's String class has limited functionality on accessing underlying buffer
 *  and resizing and should be avoided (but still possible)
 * 
 * @tparam T 
 */
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
    // otherwise we assume either STL container is used, (i.e. std::vector or std::string) or derived class should implement same methods
    if constexpr(!std::is_same_v<String, std::decay_t<decltype(container)>>)
      return container.size();
  };

  /**
   * @copydoc WSMessageGeneric::getData()
   * @details though casted to const char* the data there MIGHT not be NULL-terminated string (depending on underlying container)
   */
  const char* getData() override {
    // specialization for Arduino String
    if constexpr(std::is_same_v<String, std::decay_t<decltype(container)>>)
      return container.c_str();
    // otherwise we assume either STL container is used, (i.e. st::vector or std::string) or derived class should implement same methods
    if constexpr(!std::is_same_v<String, std::decay_t<decltype(container)>>)
      return reinterpret_cast<char*>(container.data());
  }

  /**
   * @copydoc WSMessageGeneric::getBuffer()
   * @details though casted to const char* the data there MIGHT not be NULL-terminated string (depending on underlying container)
   */
  char* getBuffer() override {
    // specialization for Arduino String - it does not allow accessing underlying buffer, so return nullptr here
    if constexpr(std::is_same_v<String, std::decay_t<decltype(container)>>)
      return nullptr;
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
    container.insert(0, (char*)(&buff), 2);
  };
  
  uint16_t getStatusCode() const override { return _status_code; }
};

/**
 * @brief Dummy message that does not carry any data
 * could be used as a container for bodyless control messages or 
 * specific cases (to gracefully handle oversised incoming messages)
 */
class WSMessageDummy : public WSMessageGeneric {
  const uint16_t _code;
public:
  explicit WSMessageDummy(WSFrameType_t type, uint16_t status_code = 0) : WSMessageGeneric(type, true), _code(status_code) {};
  size_t getSize() const override { return 0; };
  const char* getData() override { return nullptr; };
  uint16_t getStatusCode() const override { return _code; }

protected:
  void addChunk(char* data, size_t len, size_t offset) override {};
};

/**
 * @brief A message that carries a pointer to arbitrary blob of data
 * it could be used to send large blocks of memory in zero-copy mode,
 * i.e. avoid intermediary buffering copies.
 * The concern here is that pointer MUST persist for the duration of message
 * transfer period. Due to async nature it' it unknown how much time it would
 * take to complete the transfer. For this a callback function is provided
 * that triggers on object's destruction. It allows to get the event on
 * transfer completetion and (possibly) release the ponter or do something else
 * 
 */
class WSMessageStaticBlob : public WSMessageGeneric {
public:

  // callback prototype that is triggered on message destruction
  using event_cb_t = std::function<void(WSMessageStatus_t status, uint32_t token)>;

  /**
   * @brief Construct a new WSMessageStaticBlob object
   * 
   * @param type WebSocket message type
   * @param final WS final bit
   * @param blob a pointer to blob object (must be casted to const char*)
   * @param size a size of the object
   * @param cb a callback function to be call when message complete sending/errored
   * @param token a unique token to identify packet instance in callback
   */
  explicit WSMessageStaticBlob(WSFrameType_t type, bool final, const char* blob, size_t size, event_cb_t cb = {}, uint32_t token = 0)
    : WSMessageGeneric(type, final), _blob(blob), _size(size), _callback(cb), _token(token)  {};

    // d-tor, we call the callback here
  ~WSMessageStaticBlob(){ if (_callback) _callback(WSMessageStatus_t::complete, _token); }

  size_t getSize() const override { return _size; };
  const char* getData() override { return _blob; };

private:
  const char* _blob;
  size_t _size;
  event_cb_t _callback;
  // unique token identifying the packet
  uint32_t _token;
  // it is not allowed to store anything here
  void addChunk(char* data, size_t len, size_t offset) override final {};

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
    disconnected    // ws peer is disconnected
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
    ok,                 // all correct
    inQfull,            // inbound Q is full, won't receive new messages
    outQfull,           // outboud Q is full, won't send new messages
    Qsfull,             // both Qs are full
    disconnected,       // peer connection is broken
    na                  // client is not available
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
  // hashed url bound to client's request
  uint32_t _urlhash;

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

  /**
   * @brief access first avaiable message from inbound queue
   * @note this call will NOT remove message from queue but access message in-place!
   * 
   * @return WSMessagePtr if Q is empty, then empty pointer is returned
   */
  WSMessagePtr peekMessage();

  /**
   * @brief return peer connection state
   * 
   * @return conn_state_t 
   */
  conn_state_t connection() const {
    return _connection;
  }

  /**
   * @brief get client's state
   * returns error status if client is available to send/receive data
   * 
   * @return err_t 
   */
  err_t state() const;

  AsyncClient *client() {
    return _client;
  }

  const AsyncClient *client() const {
    return _client;
  }

  /**
   * @brief bind URL hash to client instance
   * a hash could be used to differentiate clients attached via various URLs
   * 
   * @param url 
   */
  void setURLHash(std::string_view url) { _urlhash = asyncsrv::hash_djb2a(url); }

  /**
   * @brief get URL hash bound to client
   * 
   * @return uint32_t 
   */
  uint32_t getURLHash() const { return _urlhash; }

  /**
   * @brief check if client's bound URL matches string
   * 
   * @param url 
   * @return true 
   * @return false 
   */
  bool matchURL(std::string_view url) { return _urlhash == asyncsrv::hash_djb2a(url); }

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

  /**
   * @brief Set the WebSOcket ping Keep A Live
   * if set, client will send pong packet it's peer periodically to keep the connection alive
   * ping does not require a reply from peer
   * 
   * @param seconds 
   */
  void setKeepAlive(size_t seconds){ _keepAlivePeriod = seconds * 1000; };

  // get keepalive value, seconds
  size_t getKeepAlive() const { return _keepAlivePeriod / 1000; };

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
  // counter for consumed data from tcp_pcbs, but delayd ack to hold the window
  size_t _pending_ack{0};

  // keepalive
  unsigned long _keepAlivePeriod{0}, _lastPong;

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

  void _keepalive();

  // AsyncTCP callbacks
  void _onTimeout(uint32_t time);
  void _onDisconnect(AsyncClient *c);
  void _onData(void *pbuf, size_t plen);
  void _onPoll(AsyncClient *c);
};

/**
 * @brief WebServer Handler implementation that plays the role of a WebSocket server
 * it inherits behavior of original WebSocket server and uses AsyncTCP's thread to
 * run callbacks on incoming messages
 * 
 */
class WSocketServer : public AsyncWebHandler {
public:
  // error enqueue to all
  enum class msgall_err_t {
    ok = 0,         // message was enqueued for delivering to all clients
    partial,        // some of clients queueus are full, message was not enqueued there
    none            // no clients available, or all outbound queues are full, message discarded
  };

  /**
   * @brief Construct a new WSocketServer object
   * 
   * @param url - URL endpoint
   * @param handler - event callback handler
   * @param msgsize - max inbound message size (8k by default)
   * @param qcap - queues size limit
   */
  explicit WSocketServer(const char* url, WSocketClient::event_cb_t handler = {}, size_t msgsize = 8 * 1024, size_t qcap = 4) : eventHandler(handler), msgsize(msgsize), qcap(qcap) { _urlhashes.push_back(asyncsrv::hash_djb2a(url)); }
  virtual ~WSocketServer(){};

  /**
   * @brief add additional URL to a list of websocket handlers
   * 
   * @param url 
   */
  void addURLendpoint(std::string_view url){ _urlhashes.push_back(asyncsrv::hash_djb2a(url)); }

  /**
   * @brief remove URL from a list of websocket handlers
   * 
   * @param url 
   */
  void removeURLendpoint(std::string_view url);

  /**
   * @brief clear list of handled URLs
   * @note new client's won't be able to connect to server unless at least 
   * one new endpoint added
   * 
   */
  void clearURLendpoints(){ _urlhashes.clear(); }

  /**
   * @copydoc WSClient::setOverflowPolicy(overflow_t policy)
   * @note default is 'disconnect'
   */
  void setOverflowPolicy(WSocketClient::overflow_t policy){ _overflow_policy = policy; }
  WSocketClient::overflow_t getOverflowPolicy() const { return _overflow_policy; }

  /**
   * @brief Set Message Size limit for the incoming messages
   * if peer tries to send us message larger then defined limit size,
   * the message will be discarded and peer's connection would be closed with respective error code
   * @note only new connections would be affected with changed value
   * 
   * @param size 
   */
  void setMaxMessageSize(size_t size){ msgsize = size; }
  size_t getMaxMessageSize(size_t size) const { return msgsize; }

  /**
   * @brief Set in/out Message Queue Size
   * 
   * @param size 
   */
  void setMessageQueueSize(size_t size){ qcap = size; }
  size_t getMessageQueueSize(size_t size) const { return qcap; }

  /**
   * @brief Set the WebSocket client Keep A Live
   * if set, server will pong it's peers periodically to keep connections alive
   * @note it does not check for replies and it's validity, it only sends messages to
   * help keep TCP connection alive through firewalls/routers
   * 
   * @param seconds 
   */
  void setKeepAlive(size_t seconds){ _keepAlivePeriod = seconds; };

  // get keepalive value
  size_t getKeepAlive() const { return _keepAlivePeriod; };

  /**
   * @brief activate server-side message echo
   * when activated server will echo incoming messages from any client to all other connected clients.
   * This could be usefull for applications that share messages between all connected clients, i.e. WebUIs
   * to reflect controls across all connected clients
   * @note only messages with text and binary types are echoed, control messages are not echoed
   *
   * @param enabled
   * @param splitHorizon - when true echo message to all clients but the one from where message was received,
   * when false, echo back message to all clients include the one who sent it
   */
  void setServerEcho(bool enabled = true, bool splitHorizon = true){ _serverEcho = enabled, _serverEchoSplitHorizon = splitHorizon; };

  // get server echo mode
  bool getServerEcho() const { return _serverEcho; };
  

  /**
   * @brief check if client with specified id can accept new message for sending
   * 
   * @param id 
   * @return WSocketClient::err_t - ready to send only if returned value is err_t::ok, otherwise err reason is returned 
   */
  WSocketClient::err_t clientState(uint32_t id) const;

  /**
   * @brief check if all of the connected clients are available for sending data,
   * i.e. connection state available and outbound Q is not full
   * 
   * @return msgall_err_t 
   */
  msgall_err_t clientsState() const;

  // return number of active (connected) clients
  size_t activeClientsCount() const;

  // return number of active (connected) clients to specific endpoint
  size_t activeEndpointClientsCount(std::string_view endpoint) const { return activeEndpointClientsCount(asyncsrv::hash_djb2a(endpoint)); }
  size_t activeEndpointClientsCount(uint32_t hash) const;

  /**
   * @brief Get ptr to client with specified id
   * 
   * @param id 
   * @return WSocketClient* - nullptr if client not found
   */
  WSocketClient* getClient(uint32_t id);
  WSocketClient const* getClient(uint32_t id) const;

  // find if there is a client with specified id
  bool hasClient(uint32_t id) const {
    return getClient(id) != nullptr;
  }

  /**
   * @brief disconnect client
   * 
   * @param id 
   * @param code 
   * @param message 
   */
  WSocketClient::err_t close(uint32_t id, uint16_t code = 1000, const char* message = NULL){
    if (WSocketClient* c = getClient(id))
      return c->close(code, message);
    else return WSocketClient::err_t::na;
  }

  /**
   * @brief disconnect all clients
   * 
   * @param code 
   * @param message 
   */
  void closeAll(uint16_t code = 1000, const char* message = NULL){ for (auto &c : _clients) { c.close(code, message); } }

  /**
   * @brief sned ping to client
   * 
   * @param id 
   * @param data 
   * @param len 
   * @return true 
   * @return false 
   */
  WSocketClient::err_t ping(uint32_t id, const char* data = NULL, size_t len = 0){
    if (WSocketClient *c = getClient(id))
      return c->ping(data, len);
    else return WSocketClient::err_t::na;
  }

  /**
   * @brief send ping to all clients
   * 
   * @param data 
   * @param len 
   * @return msgall_err_t 
   */
  msgall_err_t pingAll(const char* data = NULL, size_t len = 0);

  /**
   * @brief send generic message to specific client
   * 
   * @param id 
   * @param m 
   * @return WSocketClient::err_t 
   */
  WSocketClient::err_t message(uint32_t clientid, WSMessagePtr m);

  /**
   * @brief Send message to all clients bound to specified endpoint
   * 
   * @param hash endpoint hash
   * @param m message
   * @return WSocketClient::err_t 
   */
  msgall_err_t messageToEndpoint(uint32_t hash, WSMessagePtr m);

  /**
   * @brief Send message to all clients bound to specified endpoint
   * 
   * @param urlpath endpoint path
   * @param m message
   * @return WSocketClient::err_t 
   */
  msgall_err_t messageToEndpoint(std::string_view urlpath, WSMessagePtr m){ return messageToEndpoint(asyncsrv::hash_djb2a(urlpath) , m); };

  /**
   * @brief send generic message to all available clients
   * 
   * @param m 
   * @return msgall_err_t 
   */
  msgall_err_t messageAll(WSMessagePtr m);

  /**
   * @brief Send text message to client
   * this template can accept anything that std::string can be made of
   * 
   * @tparam Args 
   * @param id 
   * @param args 
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  WSocketClient::err_t text(uint32_t id, Args&&... args){
    if (hasClient(id))
      return message(id, std::make_shared<WSMessageContainer<std::string>>(WSFrameType_t::text, true, std::forward<Args>(args)...));
    else return WSocketClient::err_t::na;
  }

  /**
   * @brief Send text message to all clients in specified endpoint
   * this template can accept anything that std::string can be made of
   * 
   * @tparam Args 
   * @param hash urlpath hash to send to
   * @param args 
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  msgall_err_t textToEndpoint(uint32_t hash, Args&&... args){
    return messageToEndpoint(hash, std::make_shared<WSMessageContainer<std::string>>(WSFrameType_t::text, true, std::forward<Args>(args)...));
  }

  template<typename... Args>
  msgall_err_t textToEndpoint(std::string_view urlpath, Args&&... args){
    return textToEndpoint(asyncsrv::hash_djb2a(urlpath), std::forward<Args>(args)...);
  }

  /**
   * @brief Send text message to all avalable clients
   * this template can accept anything that std::string can be made of
   * 
   * @tparam Args 
   * @param args 
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  msgall_err_t textAll(Args&&... args){
    return messageAll(std::make_shared<WSMessageContainer<std::string>>(WSFrameType_t::text, true, std::forward<Args>(args)...));
  }

  /**
   * @brief Send String text message to client
   * this template can accept anything that Arduino String can be made of
   * 
   * @tparam Args Arduino String constructor arguments
   * @param id clien id to send message to
   * @param args 
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  WSocketClient::err_t string(uint32_t id, Args&&... args){
    if (hasClient(id))
      return message(std::make_shared<WSMessageContainer<String>>(WSFrameType_t::text, true, std::forward<Args>(args)...));
    else return WSocketClient::err_t::na;
  }

  /**
   * @brief Send String text message to all clients in specified endpoint
   * this template can accept anything that Arduino String can be made of
   * 
   * @tparam Args
   * @param hash urlpath hash to send to
   * @param args Arduino String constructor arguments
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  msgall_err_t stringToEndpoint(uint32_t hash, Args&&... args){
    return messageToEndpoint(hash, std::make_shared<WSMessageContainer<String>>(WSFrameType_t::text, true, std::forward<Args>(args)...));
  }

  template<typename... Args>
  msgall_err_t stringToEndpoint(std::string_view urlpath, Args&&... args){
    return stringToEndpoint(asyncsrv::hash_djb2a(urlpath), std::forward<Args>(args)...);
  }

  /**
   * @brief Send String text message all avalable clients
   * this template can accept anything that Arduino String can be made of
   * 
   * @tparam Args
   * @param id client id to send message to
   * @param args Arduino String constructor arguments
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  msgall_err_t stringAll(Args&&... args){
    return messageAll(std::make_shared<WSMessageContainer<String>>(WSFrameType_t::text, true, std::forward<Args>(args)...));
  }

  /**
   * @brief Send binary message to client
   * this template can accept anything that std::vector can be made of
   * 
   * @tparam Args 
   * @param id client id to send message to
   * @param args std::vector constructor arguments
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  WSocketClient::err_t binary(uint32_t id, Args&&... args){
    if (hasClient(id))
      return message(id, std::make_shared< WSMessageContainer<std::vector<uint8_t>> >(WSFrameType_t::binary, true, std::forward<Args>(args)...));
    else return WSocketClient::err_t::na;
  }

  /**
   * @brief Send binary message to all clients in specified endpoint
   * this template can accept anything that std::vector can be made of
   * 
   * @tparam Args
   * @param hash urlpath hash to send to
   * @param args std::vector constructor arguments
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  msgall_err_t binaryToEndpoint(uint32_t hash, Args&&... args){
    return messageToEndpoint(std::make_shared< WSMessageContainer<std::vector<uint8_t>> >(hash, WSFrameType_t::binary, true, std::forward<Args>(args)...));
  }

  template<typename... Args>
  msgall_err_t binaryToEndpoint(std::string_view urlpath, Args&&... args){
    return binaryToEndpoint(std::make_shared< WSMessageContainer<std::vector<uint8_t>> >(asyncsrv::hash_djb2a(urlpath), WSFrameType_t::binary, true, std::forward<Args>(args)...));
  }

  /**
   * @brief Send binary message all avalable clients
   * this template can accept anything that std::vector can be made of
   * 
   * @tparam Args
   * @param id client id to send message to
   * @param args std::vector constructor arguments
   * @return WSocketClient::err_t 
   */
  template<typename... Args>
  msgall_err_t binaryAll(Args&&... args){
    return messageAll(std::make_shared< WSMessageContainer<std::vector<uint8_t>> >(WSFrameType_t::binary, true, std::forward<Args>(args)...));
  }

  // set webhanshake handler
  void handleHandshake(AwsHandshakeHandler handler) {
    _handshakeHandler = handler;
  }

  /**
   * @brief callback for AsyncServer - onboard new ws client
   * 
   * @param request 
   * @return true 
   * @return false 
   */
  virtual bool newClient(AsyncWebServerRequest* request);

protected:
  // a list of url hashes this server is bound to
  std::vector<uint32_t> _urlhashes;
  // WSocketClient events handler
  WSocketClient::event_cb_t eventHandler;

  std::list<WSocketClient> _clients;
  #ifdef ESP32
  std::mutex clientslock;
  #endif
  unsigned long _keepAlivePeriod{0};
  // max message size
  size_t msgsize;
  // client's queue capacity
  size_t qcap;

  // return next available client's ID
  uint32_t getNextId() {
    return ++_cNextId;
  }

  void serverEcho(WSocketClient *c);

  /**
   * @brief go through clients list and remove those ones that are disconnected and have no messages pending
   * 
   */
  void _purgeClients();

private:
  AwsHandshakeHandler _handshakeHandler;
  uint32_t _cNextId{0};
  WSocketClient::overflow_t _overflow_policy{WSocketClient::overflow_t::disconnect};
  bool _serverEcho{false}, _serverEchoSplitHorizon;


  // WebServer methods
  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;
};

/**
 * @brief WebServer Handler implementation that plays the role of a WebSocket server
 * 
 */
class WSocketServerWorker : public WSocketServer {
public:

  // message callback alias
  using msg_cb_t = std::function<void(WSMessagePtr msg, uint32_t client_id)>;

  explicit WSocketServerWorker(const char* url, msg_cb_t msg_handler, WSocketClient::event_cb_t event_handler, size_t msgsize = 8 * 1024, size_t qcap = 4)
    : WSocketServer(url, nullptr, msgsize, qcap), _mcb(msg_handler), _ecb(event_handler) {}

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
  void setEventHandler(WSocketClient::event_cb_t handler){ _ecb = handler; };

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
  WSocketClient::event_cb_t _ecb;
  // worker task that handles messages
  TaskHandle_t    _task_hndlr{nullptr};
  void _taskRunner();

};

#endif  // ESP32
#else  // __cplusplus >= 201703L
#warning "WSocket requires C++17, won't build"
#endif // __cplusplus >= 201703L
