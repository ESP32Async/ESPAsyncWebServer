// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

// A new experimental implementation of Async WebSockets client/server

// We target C++17 capable toolchain
#if __cplusplus >= 201703L
#include "AsyncWSocket.h"
#if defined(ESP32)
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#include "literals.h"

#define WS_MAX_HEADER_SIZE  16

constexpr const char WS_STR_CONNECTION[] = "Connection";
constexpr const char WS_STR_VERSION[] = "Sec-WebSocket-Version";
constexpr const char WS_STR_KEY[] = "Sec-WebSocket-Key";
constexpr const char WS_STR_PROTOCOL[] = "Sec-WebSocket-Protocol";

// WSockServer worker task
constexpr const char WS_SRV_TASK[] = "WSSrvtask";

// cast enum class to uint (for bit set)
template <class E>
constexpr std::common_type_t<uint32_t, std::underlying_type_t<E>>
enum2uint32(E e) {
  return static_cast<std::common_type_t<uint32_t, std::underlying_type_t<E>>>(e);
}


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
  if (!client) {
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
  //log_d("send ws header, hdr size:%u, body len:%u", headLen, frame.len);
  // return size of a header added or 0 if any error
  return sent == headLen ? sent : 0;
}


// ******** WSocket classes implementation ********

WSocketClient::WSocketClient(uint32_t id, AsyncWebServerRequest *request, WSocketClient::event_cb_t call_back, size_t msgsize, size_t qcapsize) : 
  id(id),
  _client(request->client()),
  _cb(call_back),
  _max_msgsize(msgsize),
  _max_qcap(qcapsize)
{
  _lastPong = millis();
  // disable connection timeout
  _client->setRxTimeout(0);
  // disable Nagle's algo
  _client->setNoDelay(true);
  // set AsyncTCP callbacks
  _client->onAck( [](void *r, AsyncClient *c, size_t len, uint32_t rtt) { (void)c; reinterpret_cast<WSocketClient*>(r)->_clientSend(len); }, this );
  _client->onDisconnect(  [](void *r, AsyncClient *c) { reinterpret_cast<WSocketClient*>(r)->_onDisconnect(c); }, this );
  _client->onTimeout(     [](void *r, AsyncClient *c, uint32_t time) { (void)c; reinterpret_cast<WSocketClient*>(r)->_onTimeout(time); }, this );
  _client->onData(        [](void *r, AsyncClient *c, void *buf, size_t len) { (void)c; reinterpret_cast<WSocketClient*>(r)->_onData(buf, len); }, this );
  _client->onPoll(        [](void *r, AsyncClient *c) { reinterpret_cast<WSocketClient*>(r)->_onPoll(c); }, this );
  _client->onError(       [](void *r, AsyncClient *c, int8_t error) { (void)c; log_e("err:%d", error); }, this );
  // bind URL hash
  setURLHash(request->url().c_str());

  delete request;
}

WSocketClient::~WSocketClient() {
  if (_client){
    // remove callback here, 'cause _client's destructor will call it again
    _client->onDisconnect(nullptr);
    delete _client;
    _client = nullptr;
  }
  if (_eventGroup){
    vEventGroupDelete(_eventGroup);
    _eventGroup = nullptr;
  }
}

// ***** AsyncTCP callbacks *****
//#ifdef NOTHING
// callback acknowledges sending pieces of data for outgoing frame
void WSocketClient::_clientSend(size_t acked_bytes){
  if (!_client || _connection == conn_state_t::disconnected)
    return;

  /*
    this method could be called from different threads - AsyncTCP's ack/poll and user thread when enqueing messages,
    only AsyncTCP's ack is mandatory to execute since it carries acked data size, others could be ignored completely
    if this call is already exucute in progress. Worse case it will catch up later on next poll
  */

  // create lock object but don't actually take the lock yet
  std::unique_lock lock{_sendLock, std::defer_lock};

  // for response data we need to control AsyncTCP's event queue and in-flight fragmentation. Sending small chunks could give lower latency,
  // but flood asynctcp's queue and fragment socket buffer space for large responses.
  // Let's ignore polled acks and acks in case when we have more in-flight data then the available socket buff space.
  // That way we could balance on having half the buffer in-flight while another half is filling up and minimizing events in asynctcp's Q
  if (acked_bytes){
    //log_d("ack:%u/%u, sock space:%u", acked_bytes, _in_flight, sock_space);
    _in_flight -= std::min(acked_bytes, _in_flight);
    auto sock_space = _client->space();
    if (!sock_space){
      return;
    }
    //log_d("infl:%u, credits:%u", _in_flight, _in_flight_credit);
    // check if we were waiting to ack our disconnection frame
    if (!_in_flight && (_connection == conn_state_t::disconnecting)){
      //log_d("closing tcp-conn");
      // we are server, should close connection first as per https://datatracker.ietf.org/doc/html/rfc6455#section-7.1.1
      // here we close from the app side, send TCP-FIN to the party and move to FIN_WAIT_1/2 states
      _client->close();
      return;
    }

    // if it's the ack call from AsyncTCP - wait for lock!
    lock.lock();
    
  } else {
    // if there is no acked data - just quit if won't be able to grab a lock, we are already sending something
    if (!lock.try_lock())
      return;

    //auto sock_space = _client->space();
    //log_d("no ack infl:%u, space:%u, data pending:%u", _in_flight, sock_space, (uint32_t)(_outFrame.len - _outFrame.index));
    // 
    if (!_client->space())
      return;
  }

  // ignore the call if available sock space is smaller then acked data and we won't be able to fit message's ramainder there
  // this will reduce AsyncTCP's event Q pressure under heavy load
  if ((_outFrame.msg && (_outFrame.len - _outFrame.index > _client->space())) && (_client->space() < acked_bytes) ){
    //log_d("defer ws send call, in-flight:%u/%u", _in_flight, _client->space());
    return;
  }

  // no message in transit and we have enough space in sockbuff - try to evict new msg from a Q
  if (!_outFrame.msg && _client->space() > WS_MAX_HEADER_SIZE){
    if (_evictOutQueue()){
      // generate header and add to the socket buffer. todo: check returned size?
      _in_flight += webSocketSendHeader(_client, _outFrame);
    } else
      return;   // nothing to send now
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
      //size_t l = _outFrame.len;
      //log_d("add to sock:%u, fidx:%u/%u, infl:%u", payload_pend, (uint32_t)_outFrame.index, (uint32_t)_outFrame.len, _in_flight);
    }

    if (_outFrame.index == _outFrame.len){
      // if we complete writing entire message, send the frame right away
      if (!_client->send())
        _client->abort();

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

      // no use case for this for now
      //_sendEvent(event_t::msgSent);
    
      // if there is still buffer space available try to pull next msg from Q
      if (_client->space() > WS_MAX_HEADER_SIZE && _evictOutQueue()){
        // generate header and add to the socket buffer
        _in_flight += webSocketSendHeader(_client, _outFrame);
        continue;
      } else {
        return;
      }
    }

    if (_client->space() <= WS_MAX_HEADER_SIZE){
      // we have exhausted socket buffer, send it and quit
      if (!_client->send())
        _client->abort();
      return;
    }

    // othewise it was chunked message object and current chunk has been complete
    if (_outFrame.chunk_offset == _outFrame.msg->getCurrentChunk().second){
      // request a new one
      size_t next_chunk_size = _outFrame.msg->getNextChunk();
      if (next_chunk_size == 0){
        // chunk is not ready yet, need to async wait and return for data later, we quit here and reevaluate on next ack or poll event from AsyncTCP
        if (!_client->send()) _client->abort();
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
  if (_messageQueueOut.size() && _client->space() > WS_MAX_HEADER_SIZE ){
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

void WSocketClient::_onTimeout(uint32_t time) {
  if (!_client) {
    return;
  }
  // Serial.println("onTime");
  (void)time;
  _client->abort();
}

void WSocketClient::_onDisconnect(AsyncClient *c) {
  _connection = conn_state_t::disconnected;
  //log_d("TCP client disconnected");
  _sendEvent(event_t::disconnect);
}

void WSocketClient::_onData(void *pbuf, size_t plen) {
  //log_d("_onData, 0x%08" PRIx32 " len:%u", (uint32_t)pbuf, plen);
  if (!pbuf || !plen || _connection == conn_state_t::disconnected) return;
  char *data = (char *)pbuf;

  size_t pb_len = plen;

  while (plen){
    if (!_inFrame.msg){
      // it's a new frame, need to parse header data
      size_t framelen;
      uint16_t errcode;
      std::tie(framelen, errcode) = _mkNewFrame(data, plen, _inFrame);
      if (framelen < 2 || errcode){
        // got bad length or close code, initiate disconnect procedure
        #ifdef ESP32
        std::unique_lock<std::recursive_mutex> lockout(_outQlock);
        #endif
        _messageQueueOut.push_front( std::make_shared<WSMessageClose>(errcode) );
        // send disconnect message now
        _clientSend();
        return;
      }
      // receiving a new frame from here
      data += std::min(framelen, plen); // safety measure from bad parsing, we can't deduct more than sockbuff size
      plen -= std::min(framelen, plen);
    } else {
      // continuation of existing frame
      size_t payload_len = std::min(static_cast<size_t>(_inFrame.len - _inFrame.index), plen);
      // unmask the payload
      if (_inFrame.mask)
        wsMaskPayload(_inFrame.mask, _inFrame.index, static_cast<char*>(data), payload_len);

      // todo: for now assume object will consume all the payload provided
      _inFrame.msg->addChunk(data, payload_len, _inFrame.index);
      _inFrame.index += payload_len;
      data += payload_len;
      plen -= payload_len;
    }
  
    // if we got whole frame now
    if (_inFrame.index == _inFrame.len){
      //log_d("cmplt msg len:%u", (uint32_t)_inFrame.len);

      if (_inFrame.msg->getStatusCode() == 1007){
        // this is a dummy/corrupted message, we discard it
        _inFrame.msg.reset();
        continue;
      }

      switch (_inFrame.msg->type){
        // received close message
        case WSFrameType_t::close : {
          if (_connection == conn_state_t::disconnecting){
            //log_d("recv close ack");
            // if it was ws-close ack - we can close TCP connection
            _connection = conn_state_t::disconnected;
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
          //log_d("recv client's ws-close req");
          {
            #ifdef ESP32
            std::unique_lock<std::recursive_mutex> lockin(_inQlock);
            std::unique_lock<std::recursive_mutex> lockout(_outQlock);
            #endif
            // push message to recv Q if it has body, client might use it to understand disconnection reason
            if (_inFrame.len > 2)
              _messageQueueIn.push_back(_inFrame.msg);
            // purge the out Q and echo recieved frame back to client, once it's tcp-acked from the other side we can close tcp connection
            _messageQueueOut.clear();
            _messageQueueOut.push_front(_inFrame.msg);
          }
          _inFrame.msg.reset();
          // send event only when message has body
          if (_inFrame.len > 2)
            _sendEvent(event_t::msgRecv);
          break;
        }

        case WSFrameType_t::ping : {
          #ifdef ESP32
            std::unique_lock<std::recursive_mutex> lock(_outQlock);
          #endif
          // just reply to ping, does user needs this ping message?
          _messageQueueOut.emplace_front( std::make_shared<WSMessageContainer<std::string>>(WSFrameType_t::pong, true, _inFrame.msg->getData()) );
          _inFrame.msg.reset();
          // send frame is no other message is in progress
          if (!_outFrame.msg)
            _clientSend();
          break;
        }

        default: {
          // any other messages
          {
            #ifdef ESP32
            std::lock_guard<std::recursive_mutex> lock(_inQlock);
            #endif
            _messageQueueIn.push_back(_inFrame.msg);
          }
          _inFrame.msg.reset();
          _sendEvent(event_t::msgRecv);
          break;
        }
      }
    }
  }

  /*
    Applying TCP window control here. In case if there are pending messages in the Q
    we clamp window size gradually to push sending party back. The larger the Q grows
    then more window is closed. This works pretty well for messages sized about or more
    than TCP windows size (5,7k default for Arduino). It could prevent Q overflow and
    sieze incoming data flow without blocking the entie network stack. Mostly usefull
    with websocket worker where AsyncTCP thread is not blocked by user callbacks.
  */
  if (_messageQueueIn.size()){
    _client->ackLater();
    size_t reduce_size = pb_len * _messageQueueIn.size() / _max_qcap;
    _client->ack(pb_len - reduce_size);
    _pending_ack += reduce_size;
    //log_d("delay ack:%u, total pending:%u", reduce_size, _pending_ack);
  }
}

void WSocketClient::_onPoll(AsyncClient *c){
  /*
    Window control - we open window deproportionally to Q size letting data flow a bit
  */
  if (_pending_ack){
    size_t to_keep = _pending_ack * (_messageQueueIn.size() + 1) / _max_qcap;
    _client->ack(_pending_ack - to_keep);
    size_t bak = _pending_ack;
    _pending_ack = to_keep;
    //log_d("poll ack:%u, left:%u\n", bak - _pending_ack, _pending_ack);
  }
  _keepalive();
  // call send if no other message is in progress and Q is not empty somehow,
  // otherwise rely on ack events
  if (!_outFrame.msg && _messageQueueOut.size())
    _clientSend();
}

std::pair<size_t, uint16_t> WSocketClient::_mkNewFrame(char* data, size_t len, WSMessageFrame& frame){
  if (len < 2) return {0, 1002};    // return protocol error
  uint8_t opcode = data[0] & 0x0F;
  bool final = data[0] & 0x80;
  bool masked = data[1] & 0x80;

  // read frame size
  frame.len = data[1] & 0x7F;
  size_t offset = 2;  // first 2 bytes
/*
  Serial.print("ws hdr: ");
  //Serial.println(frame.mask, HEX);
  char buffer[10] = {}; // Buffer for hex conversion
  char* ptr = data;
  for (size_t i = 0; i != 10; ++i ) {
    sprintf(buffer, "%02X", *ptr); // Convert to uppercase hex
    Serial.print(buffer);
    //Serial.print(" ");
    ++ptr;
  }
  Serial.println();
*/
  // find message size from header
  if (frame.len == 126 && len >= 4) {
    // two byte
    frame.len = (data[2] << 8) | data[3];
    offset += 2;
  } else if (frame.len == 127 && len >= 10) {
    // four byte
    frame.len = ntohl(*(uint32_t*)(data + 2));  // MSB
    frame.len <<= 32;
    frame.len |= ntohl(*(uint32_t*)(data + 6)); // LSB
    offset += 8;
  }

  //log_d("recv hdr, sock data:%u, msg body size:%u", len, frame.len);

  // if ws.close() is called, Safari sends a close frame with plen 2 and masked bit set. We must not try to read mask key from beyond packet size
  if (masked && len >= offset + 4) {
    // mask bytes order are LSB, so we can copy it as-is
    frame.mask = *reinterpret_cast<uint32_t*>(data + offset);
    //Serial.printf("mask key at %u, :0x", offset);
    //Serial.println(frame.mask, HEX);
    offset += 4;
  }

  frame.index = frame.chunk_offset = 0;

  size_t bodylen = std::min(static_cast<size_t>(frame.len), len - offset);
  if (!bodylen){
    // if there is no body in message, then it must be a specific control message with no payload
    _inFrame.msg = std::make_shared<WSMessageDummy>(static_cast<WSFrameType_t>(opcode));
  } else {
    if (frame.len > _max_msgsize){
      // message is bigger than we are allowed to accept, create a dummy container for it, it will just discard all incoming data
      _inFrame.msg = std::make_shared<WSMessageDummy>(static_cast<WSFrameType_t>(opcode), 1007);    // code 'Invalid frame payload data'
      offset += bodylen;
      _inFrame.index = bodylen;
      return {offset, 1009};  // code 'message too big'
    }

    // check in-queue for overflow
    if (_messageQueueIn.size() >= _max_qcap){
      log_w("q overflow, client id:%u, qsize:%u", id, _messageQueueIn.size());
      switch (_overflow_policy){
        case overflow_t::discard :
          // silently discard incoming message
          _inFrame.msg = std::make_shared<WSMessageDummy>(static_cast<WSFrameType_t>(opcode), 1007);    // code 'Invalid frame payload data'
          offset += bodylen;
          _inFrame.index = bodylen;
          return {offset, 0};

        case overflow_t::disconnect : {
          // discard incoming message and send close message
          _inFrame.msg = std::make_shared<WSMessageDummy>(static_cast<WSFrameType_t>(opcode), 1007);    // code 'Invalid frame payload data'
          #ifdef ESP32
          std::lock_guard<std::recursive_mutex> lock(_inQlock);
          #endif
          _messageQueueOut.push_front( std::make_shared<WSMessageClose>(1011, "Server Q overflow") );
          offset += bodylen;
          _inFrame.index = bodylen;
          return {offset, 0};
        }

        case overflow_t::drophead :
          _messageQueueIn.pop_front();
          break;
        case overflow_t::droptail :
          _messageQueueIn.pop_back();
          break;

        default:;
      }

      _sendEvent(event_t::msgDropped);
    }
    
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
        if (bodylen > 2){
          // create a text message container consuming as much data as possible from current payload
          _inFrame.msg = std::make_shared<WSMessageClose>(status_code, data + offset + 2, bodylen -2);  // deduce 2 bytes of message code
        } else {
          // must be close message w/o body
          _inFrame.msg = std::make_shared<WSMessageClose>(status_code);
        }
        break;
      }

      default:
        _inFrame.msg = std::make_shared<WSMessageContainer<std::vector<uint8_t>>>(static_cast<WSFrameType_t>(opcode), bodylen);
        // copy as much data as it is available in current sock buff
        // todo: for now assume object will consume all the payload provided
        _inFrame.msg->addChunk(data + offset, bodylen, 0);

    }
    offset += bodylen;
    _inFrame.index = bodylen;
  }

  //log_e("new msg frame size:%u, bodylen:%u", offset, bodylen);
  // return the number of consumed data from input buffer
  return {offset, 0};
}

WSocketClient::err_t WSocketClient::enqueueMessage(WSMessagePtr mptr){
  if (_connection != conn_state_t::connected)
    return err_t::disconnected;

  if (_messageQueueOut.size() < _max_qcap){
    {
      #ifdef ESP32
      std::lock_guard<std::recursive_mutex> lock(_outQlock);
      #endif
      _messageQueueOut.emplace_back( std::move(mptr) );
    }
    // send frame if no other message is in progress
    if (!_outFrame.msg)
      _clientSend();
    return err_t::ok;
  }

  return err_t::outQfull;
}

WSMessagePtr WSocketClient::dequeueMessage(){
  WSMessagePtr msg;
  if (_messageQueueIn.size()){
    #ifdef ESP32
    std::unique_lock<std::recursive_mutex> lock(_inQlock);
    #endif
    msg.swap(_messageQueueIn.front());
    _messageQueueIn.pop_front();
  }
  /*
    Window control - we open window deproportionally to Q size letting data flow once a message is deQ'd
  */
  if (_pending_ack){
    if (!_messageQueueIn.size()){
      _client->ack(0xffff); // on empty Q we ack whatever is left (max TCP win size)
    } else {
      size_t ackpart =_pending_ack * (_max_qcap - _messageQueueIn.size()) / _max_qcap;
      //log_d("ackdq:%u/%u", ackpart, _pending_ack);
      _client->ack(ackpart);
      _pending_ack -= ackpart;
    }
  }
  return msg;
}

WSMessagePtr WSocketClient::peekMessage(){
  return _messageQueueIn.size() ? _messageQueueIn.front() : WSMessagePtr();
}

WSocketClient::err_t WSocketClient::state() const {
  if (_connection != conn_state_t::connected) return err_t::disconnected;
  if (_messageQueueOut.size() >= _max_qcap && _messageQueueIn.size() >= _max_qcap ) return err_t::Qsfull;
  if (_messageQueueIn.size() >= _max_qcap) return err_t::inQfull;
  if (_messageQueueOut.size() >= _max_qcap) return err_t::outQfull;
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
  // send frame if no other message is in progress
  if (!_outFrame.msg)
    _clientSend();
  return err_t::ok;
}

void WSocketClient::_sendEvent(event_t e){
  if (_eventGroup)
    xEventGroupSetBits(_eventGroup, enum2uint32(e));
  if (_cb)
    _cb(this, e);
}

void WSocketClient::_keepalive(){
  if (_keepAlivePeriod && (millis() - _lastPong > _keepAlivePeriod)){
    enqueueMessage(std::make_shared< WSMessageContainer<std::string> >(WSFrameType_t::pong, true, "WSocketClient Pong" ));
    _lastPong = millis();
  }
}


// ***** WSocketServer implementation *****

bool WSocketServer::newClient(AsyncWebServerRequest *request){
  // remove expired clients first
  _purgeClients();
  {
    #ifdef ESP32
    std::lock_guard<std::mutex> lock(clientslock);
    #endif
    _clients.emplace_back(getNextId(), request,
      [this](WSocketClient *c, WSocketClient::event_t e){
        // server echo call
        if (e == WSocketClient::event_t::msgRecv) serverEcho(c);
        if (eventHandler)
          eventHandler(c, e);
        else
          c->dequeueMessage(); },   // silently discard incoming messages when there is no callback set
      msgsize, qcap);
    _clients.back().setOverflowPolicy(_overflow_policy);
    _clients.back().setKeepAlive(_keepAlivePeriod);
  }
  if (eventHandler)
    eventHandler(&_clients.back(), WSocketClient::event_t::connect);
  return true;
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

bool WSocketServer::canHandle(AsyncWebServerRequest *request) const {
  if (request->isWebSocketUpgrade()){
    auto url = request->url().c_str();
    auto i = std::find_if(_urlhashes.cbegin(), _urlhashes.cend(), [url](auto const &h){ return h == asyncsrv::hash_djb2a(url); });
    return (i != _urlhashes.cend());
  }
  return false;
};

WSocketClient* WSocketServer::getClient(uint32_t id) {
  auto iter = std::find_if(_clients.begin(), _clients.end(), [id](const WSocketClient &c) { return c.id == id; });
  if (iter != std::end(_clients))
    return &(*iter);
  else
    return nullptr;
}

WSocketClient const* WSocketServer::getClient(uint32_t id) const {
  const auto iter = std::find_if(_clients.cbegin(), _clients.cend(), [id](const WSocketClient &c) { return c.id == id; });
  if (iter != std::cend(_clients))
    return &(*iter);
  else
    return nullptr;
}

WSocketClient::err_t WSocketServer::clientState(uint32_t id) const {
  if (auto c = getClient(id))
    return c->state();
  else
    return WSocketClient::err_t::disconnected;
};

WSocketServer::msgall_err_t WSocketServer::clientsState() const {
  size_t cnt = std::count_if(std::cbegin(_clients), std::cend(_clients), [](const WSocketClient &c) { return c.state() == WSocketClient::err_t::ok; });
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

WSocketClient::err_t WSocketServer::message(uint32_t clientid, WSMessagePtr m){
if (WSocketClient *c = getClient(clientid))
  return c->enqueueMessage(std::move(m));
else
  return WSocketClient::err_t::disconnected;
}

WSocketServer::msgall_err_t WSocketServer::messageAll(WSMessagePtr m){
  size_t cnt{0};
  for (auto &c : _clients) {
    if ( c.enqueueMessage(m) == WSocketClient::err_t::ok)
      ++cnt;
  }
  if (!cnt)
    return msgall_err_t::none;
  return cnt == _clients.size() ? msgall_err_t::ok : msgall_err_t::partial;
}

WSocketServer::msgall_err_t WSocketServer::messageToEndpoint(uint32_t hash, WSMessagePtr m){
  size_t cnt{0}, cntt{0};
  for (auto &c : _clients){
    if (c.getURLHash() == hash){
      ++cntt;
      if ( c.enqueueMessage(m) == WSocketClient::err_t::ok)
        ++cnt;
    }
  }
  if (!cnt)
    return msgall_err_t::none;
  return cnt == cntt ? msgall_err_t::ok : msgall_err_t::partial;
}

void WSocketServer::_purgeClients(){
  std::lock_guard lock(clientslock);
  // purge clients that are disconnected and with all messages consumed
  _clients.remove_if([](const WSocketClient& c){ return (c.connection() == WSocketClient::conn_state_t::disconnected && !c.inQueueSize() ); });
}

size_t WSocketServer::activeClientsCount() const {
  return std::count_if(std::begin(_clients), std::end(_clients),
    [](const WSocketClient &c) { return c.connection() == WSocketClient::conn_state_t::connected; }
  );
}

size_t WSocketServer::activeEndpointClientsCount(uint32_t hash) const {
  return std::count_if(std::begin(_clients), std::end(_clients),
    [hash](const WSocketClient &c) { return c.connection() == WSocketClient::conn_state_t::connected && c.getURLHash() == hash; }
  );
}

void WSocketServer::serverEcho(WSocketClient *c){
  if (!_serverEcho) return;
  auto m = c->peekMessage();
  if (m && (m->type == WSFrameType_t::text || m->type == WSFrameType_t::binary) ){
    // echo only text or bin messages
    for (auto &i: _clients){
      if (!_serverEchoSplitHorizon || i.id != c->id){
        i.enqueueMessage(m);
      }
    }
  }
}

void WSocketServer::removeURLendpoint(std::string_view url){
  _urlhashes.erase(remove_if(_urlhashes.begin(), _urlhashes.end(), [url](auto const &v){ return v == asyncsrv::hash_djb2a(url); }), _urlhashes.end());
}


// ***** WSMessageClose implementation *****

WSMessageClose::WSMessageClose (uint16_t status) : WSMessageContainer<std::string>(WSFrameType_t::close, true), _status_code(status) {
  // convert code to message body
  uint16_t buff = htons (status);
  container.append((char*)(&buff), 2);
};


// ***** WSocketServerWorker implementation *****

bool WSocketServerWorker::newClient(AsyncWebServerRequest *request){
  {
    #ifdef ESP32
    std::lock_guard<std::mutex> lock (clientslock);
    #endif
    _clients.emplace_back(getNextId(), request,
      [this](WSocketClient *c, WSocketClient::event_t e){
        //log_d("client event id:%u state:%u", c->id, c->state());
        // server echo call
        if (e == WSocketClient::event_t::msgRecv) serverEcho(c);
        if (_task_hndlr) xTaskNotifyGive(_task_hndlr);
      },
      msgsize, qcap);

    // create events group where we'll pick events
    _clients.back().createEventGroupHandle();
    _clients.back().setOverflowPolicy(getOverflowPolicy());
    _clients.back().setKeepAlive(_keepAlivePeriod);
    _clients.back().setURLHash(request->url().c_str());
    xEventGroupSetBits(_clients.back().getEventGroupHandle(), enum2uint32(WSocketClient::event_t::connect));
  }
  if (_task_hndlr)
    xTaskNotifyGive(_task_hndlr);
  return true;
}

void WSocketServerWorker::start(uint32_t stack, UBaseType_t uxPriority, BaseType_t xCoreID){
  if (_task_hndlr) return;    // we are already running

  xTaskCreatePinnedToCore(
    [](void* pvParams){ static_cast<WSocketServerWorker*>(pvParams)->_taskRunner(); },
    WS_SRV_TASK,
    stack,
    (void *)this,
    uxPriority,
    &_task_hndlr,
    xCoreID ); // == pdPASS;
}

void WSocketServerWorker::stop(){
  if (_task_hndlr){
    vTaskDelete(_task_hndlr);
    _task_hndlr = nullptr;
  }
}

void WSocketServerWorker::_taskRunner(){
  for (;;){

    // go through our client's list looking for pending events, do not care to lock the list here,
    // 'cause nobody would be able to remove anything from it but this loop and adding new client won't invalidate current iterator
    auto it = _clients.begin();
    while (it != _clients.end()){
      EventBits_t uxBits;

      // check if this a new client
      uxBits = xEventGroupClearBits(it->getEventGroupHandle(), enum2uint32(WSocketClient::event_t::connect) );
      if ( uxBits & enum2uint32(WSocketClient::event_t::connect) ){
        _ecb(&(*it), WSocketClient::event_t::connect);
      }

      // check if 'inbound Q full' flag set
      uxBits = xEventGroupClearBits(it->getEventGroupHandle(), enum2uint32(WSocketClient::event_t::inQfull) );
      if ( uxBits & enum2uint32(WSocketClient::event_t::inQfull) ){
        _ecb(&(*it), WSocketClient::event_t::inQfull);
      }

      // check for dropped messages flag
      uxBits = xEventGroupClearBits(it->getEventGroupHandle(), enum2uint32(WSocketClient::event_t::msgDropped) );
      if ( uxBits & enum2uint32(WSocketClient::event_t::msgDropped) ){
        _ecb(&(*it), WSocketClient::event_t::msgDropped);
      }

      // process all the messages from inbound Q
      xEventGroupClearBits(it->getEventGroupHandle(), enum2uint32(WSocketClient::event_t::msgRecv) );
      while (auto m{it->dequeueMessage()}){
        _mcb(m, it->id);
      }

      // check for disconnected client - do not care for group bits, cause if it's deleted, we will destruct the client object
      if (it->connection() == WSocketClient::conn_state_t::disconnected){
        // run a callback
        _ecb(&(*it), WSocketClient::event_t::disconnect);
        {
          #ifdef ESP32
            std::lock_guard<std::mutex> lock (clientslock);
          #endif
          it = _clients.erase(it);
        }
      } else {
        // advance iterator
        ++it;
      }
    }

    // wait for next event here, using counted notification we could do some dry-runs but won't miss any events
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

    // end of a task loop
  }
  vTaskDelete(NULL);
}

#endif  // (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#endif  // defined(ESP32)
#endif  // __cplusplus >= 201703L
