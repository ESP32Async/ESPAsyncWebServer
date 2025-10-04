// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

// A new experimental implementation of Async WebSockets client/server

#include "AsyncWSocket.h"
#include "literals.h"

constexpr const char WS_STR_CONNECTION[] = "Connection";
//constexpr const char WS_STR_UPGRADE[] = "Upgrade";
//constexpr const char WS_STR_ORIGIN[] = "Origin";
//constexpr const char WS_STR_COOKIE[] = "Cookie";
constexpr const char WS_STR_VERSION[] = "Sec-WebSocket-Version";
constexpr const char WS_STR_KEY[] = "Sec-WebSocket-Key";
constexpr const char WS_STR_PROTOCOL[] = "Sec-WebSocket-Protocol";
//constexpr const char WS_STR_ACCEPT[] = "Sec-WebSocket-Accept";


/**
 * @brief apply mask key to supplied data byte-per-byte
 * this is used for unaligned portions of data buffer where 32 bit ops can't be applied
 * this function takes mask offset to rollover the key bytes
 *
 * @param mask - mask key
 * @param mask_offset - offset byte for mask
 * @param data - data to apply
 * @param length - data block length
 */
inline void wsMaskPayloadPerByte(uint32_t mask, size_t mask_offset, char *data, size_t length) {
    for (char* ptr = data; ptr != data + length; ++ptr) {
      *ptr ^= reinterpret_cast<uint8_t*>(&mask)[mask_offset++];  // roll mask bytes
      if (mask_offset == sizeof(mask))
        mask_offset = 0;
    }
  }
  
/**
 * @brief apply mask key to supplied data using 32 bit XOR
 * 
 * @param mask - mask key
 * @param mask_offset - offset byte for mask
 * @param data - data to apply
 * @param length - data block length
 */
void wsMaskPayload(uint32_t mask, size_t mask_offset, char *data, size_t length) {
  /*
    we could benefit from 32-bit xor to unmask the data. The great thing of esp32 is that it could do unaligned 32 bit memory access
    while some other MCU does not (yes, RP2040, I'm talking about you!) So have to go hard way - do all operations 32-bit aligned to cover all supported MCUs
  */

  // If data size is so small that it does not make sense to use 32 bit aligned calculations, just use byte-by-byte version
  if (length < 4 * sizeof(mask)){
    wsMaskPayloadPerByte(mask, mask_offset % 4, data, length);
  } else {
    // do 32-bit vectored calculations

    // get unaligned part size
    const size_t head_remainder = reinterpret_cast<size_t>(data) % sizeof(mask);
    // set aligned head
    char* data_aligned_head = head_remainder == 0 ? data : (data + sizeof(mask) - head_remainder);
    // set aligned tail
    char* const data_end = data + length;
    char* const data_aligned_end = data_end - reinterpret_cast<size_t>(data_end) % sizeof(mask);

    // unmask unaligned part at the begining
    if (head_remainder)
      wsMaskPayloadPerByte(mask, mask_offset % 4, data, head_remainder);

    // need a derived mask key in a 32 bit var which is rolled over by the appropriate offset for data position, our byte-by-byte function could help here to derive key
    uint32_t shifted_mask{0};
    wsMaskPayloadPerByte(mask, (mask_offset + data_aligned_head - data) % sizeof(mask), reinterpret_cast<char*>(&shifted_mask), sizeof(mask));

    // (un)mask the payload
    do {
      *reinterpret_cast<uint32_t*>(data_aligned_head) ^= shifted_mask;
      data_aligned_head += sizeof(mask);
    } while(data_aligned_head != data_aligned_end);

    // unmask the unalined remainder
    wsMaskPayloadPerByte(mask, (mask_offset + (data_aligned_end - data)) % sizeof(mask), data_aligned_end, data_end - data_aligned_end);
  }
}

size_t webSocketSendHeader(AsyncClient *client, WSMessageFrame& frame) {
  if (!client || !client->canSend()) {
    return 0;
  }

  size_t headLen = 2;
  if (frame.len > 65535){
    headLen += 8;
  } else if (frame.len > 125) {
    headLen += 2;
  }
  if (frame.len && frame.mask) {
    headLen += 4;
  }

  size_t space = client->space();
  if (space < headLen) {
    // Serial.println("SF 2");
    return 0;
  }
  space -= headLen;

  // header buffer
  uint8_t buf[headLen];

  buf[0] = static_cast<uint8_t>(frame.msg->type) & 0x0F;
  if (frame.msg->final()) {
    buf[0] |= 0x80;
  }
  if (frame.len < 126) {
    // 1 byte len
    buf[1] = frame.len & 0x7F;
  } else if (frame.len > 65535){
    // 8 byte len
    buf[1] = 127;
    uint32_t lenl = htonl(frame.len & 0xffffffff);
    uint32_t lenh = htonl(frame.len >> 32);
    memcpy(buf+2, &lenh, sizeof(lenh));
    memcpy(buf+6, &lenl, sizeof(lenl));
  } else {
    // 2 byte len
    buf[1] = 126;
    *(uint16_t*)(buf+2) = htons(frame.len & 0xffff);
  }

  if (frame.len && frame.mask) {
    buf[1] |= 0x80;
    memcpy(buf + (headLen - sizeof(frame.mask)), &frame.mask, sizeof(frame.mask));
  }

  size_t sent = client->add((const char*)buf, headLen);

  if (frame.msg->type == WSFrameType_t::close && frame.msg->getStatusCode()){
    // this is a 'close' message with status code, need to send the code also along with header
    uint16_t code = htons (frame.msg->getStatusCode());
    sent += client->add((char*)(&code), 2);
    headLen += 2;
  }

  // return size of a header added or 0 if any error
  return sent == headLen ? sent : 0;
}


// ******** WSocket classes implementation ********

WSocketClient::WSocketClient(uint32_t id, AsyncWebServerRequest *request, WSocketClient::event_callback_t call_back, size_t msgsize, size_t qcapsize) : 
  id(id),
  _client(request->client()),
  _cb(call_back),
  _max_msgsize(msgsize),
  _max_qcap(qcapsize)
{
  // disable connection timeout
  _client->setRxTimeout(0);
  _client->setNoDelay(true);
  // set AsyncTCP callbacks
  _client->onAck( [](void *r, AsyncClient *c, size_t len, uint32_t rtt) { (void)c; reinterpret_cast<WSocketClient*>(r)->_clientSend(len); }, this );
  //_client->onAck( [](void *r, AsyncClient *c, size_t len, uint32_t rtt) { (void)c; reinterpret_cast<WSocketClient*>(r)->_onAck(len, rtt); }, this );
  _client->onDisconnect( [](void *r, AsyncClient *c) { reinterpret_cast<WSocketClient*>(r)->_onDisconnect(c); }, this );
  _client->onTimeout( [](void *r, AsyncClient *c, uint32_t time) { (void)c; reinterpret_cast<WSocketClient*>(r)->_onTimeout(time); }, this );
  _client->onData( [](void *r, AsyncClient *c, void *buf, size_t len) { (void)c; reinterpret_cast<WSocketClient*>(r)->_onData(buf, len); }, this );
  _client->onPoll( [](void *r, AsyncClient *c) { (void)c; reinterpret_cast<WSocketClient*>(r)->_clientSend(); }, this );
  // not implemented yet
  //_client->onError( [](void *r, AsyncClient *c, int8_t error) { (void)c; reinterpret_cast<WSocketClient*>(r)->_onError(error); }, this );
  delete request;
}

WSocketClient::~WSocketClient() {
  if (_client){
    delete _client;
    _client = nullptr;
  }
}

// ***** AsyncTCP callbacks *****
//#ifdef NOTHING
// callback acknowledges sending pieces of data for outgoing frame
void WSocketClient::_clientSend(size_t acked_bytes){
  if (!_client || _connection == conn_state_t::disconnected || !_client->space())
    return;

  /*
    this method could be called from different threads - AsyncTCP's ack/poll and user thread when enqueing messages,
    only AsyncTCP's ack is mandatory to execute since it carries acked data size, others could be ignored completely
    if this call is already exucute in progress. Worse case it will catch up later on next poll
  */

  // create lock object but don't actually take the lock yet
  std::unique_lock lock{_sendLock, std::defer_lock};

  if (acked_bytes){
    // if it's the ack call from AsyncTCP - wait for lock!
    lock.lock();
    log_d("_clientSend, ack:%u/%u, space:%u", acked_bytes, _in_flight, _client ? _client->space() : 0);
  } else {
    // if there is no acked data - just quit, we are already sending something
    if (!lock.try_lock())
      return;
  }

  // for response data we need to control AsyncTCP's event queue and in-flight fragmentation. Sending small chunks could give lower latency,
  // but flood asynctcp's queue and fragment socket buffer space for large responses.
  // Let's ignore polled acks and acks in case when we have more in-flight data then the available socket buff space.
  // That way we could balance on having half the buffer in-flight while another half is filling up and minimizing events in asynctcp's Q
  if (acked_bytes){
    _in_flight -= std::min(acked_bytes, _in_flight);
    log_d("infl:%u, state:%u", _in_flight, _connection);
    // check if we were waiting to ack our disconnection frame
    if (!_in_flight && (_connection == conn_state_t::disconnecting)){
      log_d("closing tcp-conn");
      // we are server, should close connection first as per https://datatracker.ietf.org/doc/html/rfc6455#section-7.1.1
      // here we close from the app side, send TCP-FIN to the party and move to FIN_WAIT_1/2 states
      _client->close();
      return;
    }
    
    // return buffer credit on acked data
    ++_in_flight_credit;
  }

  if (_in_flight > _client->space() || !_in_flight_credit) {
    log_d("defer ws send call, in-flight:%u/%u", _in_flight, _client->space());
    return;
  }

  // no message in transit, try to evict one from a Q
  if (!_outFrame.msg){
    if (_evictOutQueue()){
      // generate header and add to the socket buffer
      _in_flight += webSocketSendHeader(_client, _outFrame);
    } else
      return; // nothing to send now
  }
  
  // if there is a pending _outFrame - send the data from there
  while (_outFrame.msg){
    if (_outFrame.index < _outFrame.len){
      size_t payload_pend = _client->add(_outFrame.msg->getCurrentChunk().first + _outFrame.chunk_offset, _outFrame.msg->getCurrentChunk().second - _outFrame.chunk_offset);
      // if no data was added to client's buffer then it's something wrong, we can't go on
      if (!payload_pend){
        _client->abort();
        return;
      }
      _outFrame.index += payload_pend;
      _outFrame.chunk_offset += payload_pend;
      _in_flight += payload_pend;
    }

    if (_outFrame.index == _outFrame.len){
      // if we complete writing entire message, send the frame right away
      // increment in-flight counter and take the credit
      if (!_client->send())
        _client->abort();
      --_in_flight_credit;

      if (_outFrame.msg->type == WSFrameType_t::close){
        // if we just sent close frame, then change client state and purge out queue, we won't transmit anything from now on
        // the connection will be terminated once all in-flight data are acked from the other side
        _connection = conn_state_t::disconnecting;
        _outFrame.msg.reset();
        _messageQueueOut.clear();
        return;
      }

      // release _outFrame message here
      _outFrame.msg.reset();

      // execute callback that a message was sent
      if (_cb)
      _cb(this, event_t::msgSent);
    
      // if there are free in-flight credits try to pull next msg from Q
      if (_in_flight_credit && _evictOutQueue()){
        // generate header and add to the socket buffer
        _in_flight += webSocketSendHeader(_client, _outFrame);
        continue;
      } else {
        return;
      }
    }

    if (!_client->space()){
      // we have exhausted socket buffer, send it and quit
      if (!_client->send())
        _client->abort();
      // take in-flight credit
      --_in_flight_credit;
      return;
    }

    // othewise it was chunked message object and current chunk has been complete
    if (_outFrame.chunk_offset == _outFrame.msg->getCurrentChunk().second){
      // request a new one
      size_t next_chunk_size = _outFrame.msg->getNextChunk();
      if (next_chunk_size == 0){
        // chunk is not ready yet, need to async wait and return for data later, we quit here and reevaluate on next ack or poll event from AsyncTCP
        if (!_client->send()) _client->abort();
        // take in-flight credit
        --_in_flight_credit;
        return;
      } else if (next_chunk_size == -1){
        // something is wrong! there would be no more chunked data but the message has not reached it's full size yet, can do nothing but close the coonections
        log_e("Chunk data is incomlete!");
        _client->abort();
        return;
      }
      // go on with a loop
    } else {
      // can't be there?
    }
  }
}

bool WSocketClient::_evictOutQueue(){
  // check if we have something in the Q and enough sock space to send a header at least
  if (_messageQueueOut.size() && _client->space() > 16 ){
    {
      #ifdef ESP32
      std::unique_lock<std::recursive_mutex> lockout(_outQlock);
      #endif
      _outFrame.msg.swap(_messageQueueOut.front());
      _messageQueueOut.pop_front();
    }
    _outFrame.chunk_offset = _outFrame.index = 0;
    _outFrame.len = _outFrame.msg->getSize();
    return true;
  }

  return false;
  // this function assumes it's callee will take care of actually sending the header and message body further
}

void WSocketClient::_onError(int8_t) {
  // Serial.println("onErr");
}

void WSocketClient::_onTimeout(uint32_t time) {
  if (!_client) {
    return;
  }
  // Serial.println("onTime");
  (void)time;
  _client->abort();
}

void WSocketClient::_onDisconnect(AsyncClient *c) {
  Serial.println("TCP client disconencted");
  delete c;
  _client = c = nullptr;
  _connection = conn_state_t::disconnected;

  #ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_outQlock);
  #endif
  // clear out Q, we won't be able to send it anyway from now on
  _messageQueueOut.clear();
  // execute callback
  if (_cb)
    _cb(this, event_t::disconnect);
}

void WSocketClient::_onData(void *pbuf, size_t plen) {
  Serial.printf("_onData, len:%u\n", plen);
  if (!pbuf || !plen || _connection == conn_state_t::disconnected) return;
  char *data = (char *)pbuf;

  while (plen){
    if (!_inFrame.msg){
      // it's a new frame, need to parse header data
      size_t framelen;
      uint16_t errcode;
      std::tie(framelen, errcode) = _mkNewFrame(data, plen, _inFrame);
      if (!framelen){
        // was unable to start receiving frame? initiate disconnect exchange
        #ifdef ESP32
        std::unique_lock<std::recursive_mutex> lockout(_outQlock);
        #endif
        _messageQueueOut.push_front( std::make_shared<WSMessageClose>(errcode) );
        // try sending disconnect message now
        _clientSend();
        return;
      }
      // receiving a new frame from here
      data += framelen;
      plen -= framelen;
    } else {
      // continuation of existing frame
      size_t payload_len = std::min(static_cast<size_t>(_inFrame.len - _inFrame.index), plen);
      // unmask the payload
      if (_inFrame.mask)
        wsMaskPayload(_inFrame.mask, _inFrame.index, static_cast<char*>(data), payload_len);

      // todo: for now assume object will consume all the payload provided
      _inFrame.msg->addChunk(data, payload_len, _inFrame.index);
      data += payload_len;
      plen -= payload_len;
    }
  
    // if we got whole frame now
    if (_inFrame.index == _inFrame.len){
      Serial.printf("_onData, cmplt msg len:%lu\n", _inFrame.len);

      switch (_inFrame.msg->type){
        // received close message
        case WSFrameType_t::close : {
          if (_connection == conn_state_t::disconnecting){
            log_d("recv close ack");
            // if it was ws-close ack - we can close TCP connection
            _connection == conn_state_t::disconnected;
            // normally we should call close() here and wait for other side also close tcp connection with TCP-FIN, but
            // for various reasons ws clients could linger connection when received TCP-FIN not closing it from the app side (even after
            // two side ws-close exchange, i.e. websocat, websocket-client)
            // This would make server side TCP to stay in FIN_WAIT_2 state quite long time, let's call abort() here instead of close(),
            // it is harsh but let other side know that nobody would talk to it any longer and let it reinstate a new connection if needed
            // anyway we won't receive/send anything due to '_connection == conn_state_t::disconnected;'
            _client->abort();
            _inFrame.msg.reset();
            return;
          }
          
          // otherwise it's a close request from a peer - echo back close message as per https://datatracker.ietf.org/doc/html/rfc6455#section-5.5.1
          {
            log_d("recv client's ws-close req");
            #ifdef ESP32
            std::unique_lock<std::recursive_mutex> lockin(_inQlock);
            std::unique_lock<std::recursive_mutex> lockout(_outQlock);
            #endif
            // push message to recv Q, client might use it to understand disconnection reason
            _messageQueueIn.push_back(_inFrame.msg);
            // purge the out Q and echo recieved frame back to client, once it's tcp-acked from the other side we can close tcp connection
            _messageQueueOut.clear();
            _messageQueueOut.push_front(_inFrame.msg);
          }
          _inFrame.msg.reset();
          if (_cb)
            _cb(this, event_t::msgRecv);

          break;
        }

        case WSFrameType_t::ping : {
          #ifdef ESP32
            std::unique_lock<std::recursive_mutex> lock(_outQlock);
          #endif
          // just reply to ping, does user needs this ping message?
          _messageQueueOut.emplace_front( std::make_shared<WSMessageContainer<std::string>>(WSFrameType_t::pong, true, _inFrame.msg->getData()) );
          _inFrame.msg.reset();
          break;
        }

        default: {
          log_e("other msg");
          // any other messages
          {
            #ifdef ESP32
            std::unique_lock<std::recursive_mutex> lock(_inQlock);
            #endif
            // check in-queue for overflow
            if (_messageQueueIn.size() >= _max_qcap){
              log_e("q overflow");
              switch (_overflow_policy){
                case overflow_t::discard :
                  // discard incoming message
                  break;
                case overflow_t::drophead :
                  _messageQueueIn.pop_front();
                  _messageQueueIn.push_back(_inFrame.msg);
                  break;
                case overflow_t::droptail :
                  _messageQueueIn.pop_back();
                  _messageQueueIn.push_back(_inFrame.msg);
                  break;
                case overflow_t::disconnect : {
                  #ifdef ESP32
                  std::unique_lock<std::recursive_mutex> lock(_inQlock);
                  #endif
                  _messageQueueOut.push_front( std::make_shared<WSMessageClose>(1011, "Server Q overflow") );
                  break;
                }
                default:;
              }

            } else {
              log_e("push new to Q");
              _messageQueueIn.push_back(_inFrame.msg);
            }
          }
          _inFrame.msg.reset();
          log_e("evt on new msg");
          if (_cb)
            _cb(this, event_t::msgRecv);

          break;
        }
      }
    }
  }

}

std::pair<size_t, uint16_t> WSocketClient::_mkNewFrame(char* data, size_t len, WSMessageFrame& frame){
  if (len < 2) return {0, 1002};    // return protocol error
  uint8_t opcode = data[0] & 0x0F;
  bool final = data[0] & 0x80;
  bool masked = data[1] & 0x80;

  // read frame size
  frame.len = data[1] & 0x7F;
  size_t offset = 2;  // first 2 bytes

  if (frame.len == 126 && len >= 4) {
    frame.len = data[3] | (uint16_t)(data[2]) << 8;
    offset += 2;
  } else if (frame.len == 127 && len >= 10) {
    frame.len = ntohl(*(uint32_t*)(data + 2));  // MSB
    frame.len <= 32;
    frame.len |= ntohl(*(uint32_t*)(data + 6)); // LSB
    offset += 8;
  }

  if (frame.len > _max_msgsize)   // message too big than we allowed to accept
    return {0, 1009};

  // if ws.close() is called, Safari sends a close frame with plen 2 and masked bit set. We must not try to read mask key from beyond packet size
  if (masked && len >= offset + 4) {
    // mask bytes order are LSB, so we can copy it as-is
    frame.mask = *reinterpret_cast<uint32_t*>(data + offset);
    Serial.printf("mask key at %u, :0x", offset);
    Serial.println(frame.mask, HEX);
    offset += 4;
  }

  frame.index = frame.chunk_offset = 0;

  size_t bodylen = std::min(static_cast<size_t>(frame.len), len - offset);
  // if there is no body in message, then it must a specific control message with no payload
  if (!bodylen){
    // I'll make an empty binary container for such messages
    _inFrame.msg = std::make_shared<WSMessageContainer<std::vector<uint8_t>>>(static_cast<WSFrameType_t>(opcode));
  } else {
    // let's unmask payload right in sock buff, later it could be consumed as raw data
    if (masked){
      wsMaskPayload(_inFrame.mask, 0, data + offset, bodylen);
    }

    // create a new container object that fits best for message type
    switch (static_cast<WSFrameType_t>(opcode)){
      case WSFrameType_t::text :
        // create a text message container consuming as much data as possible from current payload
        _inFrame.msg = std::make_shared<WSMessageContainer<std::string>>(WSFrameType_t::text, final, (char*)(data + offset), bodylen);
        break;

      case WSFrameType_t::close : {
        uint16_t status_code = ntohs(*(uint16_t*)(data + offset));
        offset += 2;
        if (bodylen > 2){
          bodylen -= 2;
          // create a text message container consuming as much data as possible from current payload
          _inFrame.msg = std::make_shared<WSMessageClose>(status_code, data + offset, bodylen);
        } else {
          // must be close message w/o body
          _inFrame.msg = std::make_shared<WSMessageClose>(status_code);
        }
        break;
      }

      default:
        _inFrame.msg = std::make_shared<WSMessageContainer<std::vector<uint8_t>>>(static_cast<WSFrameType_t>(opcode), bodylen);
        // copy data
        memcpy(_inFrame.msg->getData(), data + offset, bodylen);

    }
    offset += bodylen;
    _inFrame.index = bodylen;
  }

  log_e("new msg offset:%u, body:%u", offset, bodylen);
  // return the number of consumed data from input buffer
  return {offset, 0};
}

WSocketClient::err_t WSocketClient::enqueueMessage(std::shared_ptr<WSMessageGeneric> mptr){
  if (_connection != conn_state_t::connected)
    return err_t::disconnected;

  if (_messageQueueOut.size() < _max_qcap){
    #ifdef ESP32
    std::lock_guard<std::recursive_mutex> lock(_outQlock);
    #endif
    _messageQueueOut.emplace_back( std::move(mptr) );
    _clientSend();
    return err_t::ok;
  }

  return err_t::nospace;
}

std::shared_ptr<WSMessageGeneric> WSocketClient::dequeueMessage(){
  #ifdef ESP32
  std::unique_lock<std::recursive_mutex> lock(_inQlock);
  #endif
  std::shared_ptr<WSMessageGeneric> msg;
  if (_messageQueueIn.size()){
    msg.swap(_messageQueueIn.front());
    _messageQueueIn.pop_front();
  }
  return msg;
}

WSocketClient::err_t WSocketClient::canSend() const {
  if (_connection != conn_state_t::connected) return err_t::disconnected;
  if (_messageQueueOut.size() >= _max_qcap ) return err_t::nospace;
  return err_t::ok;
}

WSocketClient::err_t WSocketClient::close(uint16_t code, const char *message){
  if (_connection != conn_state_t::connected)
    return err_t::disconnected;

  #ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_outQlock);
  #endif
  if (message)
    _messageQueueOut.emplace_front( std::make_shared<WSMessageClose>(code, message) );
  else
    _messageQueueOut.emplace_front( std::make_shared<WSMessageClose>(code) );
  _clientSend();
  return err_t::ok;
}

// ***** WSocketServer implementation *****


bool WSocketServer::newClient(AsyncWebServerRequest *request){
  // remove expired clients first
  _purgeClients();
  {
    #ifdef ESP32
    std::lock_guard<std::mutex> lock (_lock);
    #endif
    _clients.emplace_back(getNextId(), request, [this](WSocketClient *c, WSocketClient::event_t e){ _clientEvents(c, e); });
  }
  _clientEvents(&_clients.back(), WSocketClient::event_t::connect);
  return true;
}

void WSocketServer::_clientEvents(WSocketClient *client, WSocketClient::event_t event){
  log_d("_clientEvents: %u", event);
  if (_eventHandler)
    _eventHandler(client, event);
}

void WSocketServer::handleRequest(AsyncWebServerRequest *request) {
  if (!request->hasHeader(WS_STR_VERSION) || !request->hasHeader(WS_STR_KEY)) {
    request->send(400);
    return;
  }
  if (_handshakeHandler != nullptr) {
    if (!_handshakeHandler(request)) {
      request->send(401);
      return;
    }
  }
  const AsyncWebHeader *version = request->getHeader(WS_STR_VERSION);
  if (version->value().compareTo(asyncsrv::T_13) != 0) {
    AsyncWebServerResponse *response = request->beginResponse(400);
    response->addHeader(WS_STR_VERSION, asyncsrv::T_13);
    request->send(response);
    return;
  }
  const AsyncWebHeader *key = request->getHeader(WS_STR_KEY);
  AsyncWebServerResponse *response = new AsyncWebSocketResponse(key->value(), [this](AsyncWebServerRequest *r){ return newClient(r); });
  if (response == NULL) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    request->abort();
    return;
  }
  if (request->hasHeader(WS_STR_PROTOCOL)) {
    const AsyncWebHeader *protocol = request->getHeader(WS_STR_PROTOCOL);
    // ToDo: check protocol
    response->addHeader(WS_STR_PROTOCOL, protocol->value());
  }
  request->send(response);
}

WSocketClient* WSocketServer::_getClient(uint32_t id) {
  auto iter = std::find_if(_clients.begin(), _clients.end(), [id](const WSocketClient &c) { return c.id == id; });
  if (iter != std::end(_clients))
    return &(*iter);
  else
    return nullptr;
}

WSocketClient const* WSocketServer::_getClient(uint32_t id) const {
  const auto iter = std::find_if(_clients.cbegin(), _clients.cend(), [id](const WSocketClient &c) { return c.id == id; });
  if (iter != std::cend(_clients))
    return &(*iter);
  else
    return nullptr;
}

WSocketServer::msgall_err_t WSocketServer::canSend() const {
  size_t cnt = std::count_if(std::begin(_clients), std::end(_clients), [](const WSocketClient &c) { return c.canSend() == WSocketClient::err_t::ok; });
  if (!cnt) return msgall_err_t::none;
  return cnt == _clients.size() ? msgall_err_t::ok : msgall_err_t::partial;
}

WSocketServer::msgall_err_t WSocketServer::pingAll(const char *data, size_t len){
  size_t cnt{0};
  for (auto &c : _clients) {
    if ( c.ping(data, len) == WSocketClient::err_t::ok)
      ++cnt;
  }
  if (!cnt)
    return msgall_err_t::none;
  return cnt == _clients.size() ? msgall_err_t::ok : msgall_err_t::partial;
}

WSocketServer::msgall_err_t WSocketServer::messageAll(std::shared_ptr<WSMessageGeneric> m){
  size_t cnt{0};
  for (auto &c : _clients) {
    if ( c.enqueueMessage(m) == WSocketClient::err_t::ok)
      ++cnt;
  }
  if (!cnt)
    return msgall_err_t::none;
  return cnt == _clients.size() ? msgall_err_t::ok : msgall_err_t::partial;
}

void WSocketServer::_purgeClients(){
  log_d("purging clients");
  std::lock_guard lock(_lock);
  // purge clients that are disconnected and with all messages consumed
  std::erase_if(_clients, [](const WSocketClient& c){ return (c.status() == WSocketClient::conn_state_t::disconnected && !c.inQueueSize() ); });
}
