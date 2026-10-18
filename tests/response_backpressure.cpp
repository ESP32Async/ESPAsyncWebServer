// SPDX-License-Identifier: LGPL-3.0-or-later
// Host regression harness. The runner inserts the ACTUAL upstream/patched
// AsyncAbstractResponse::write_send_buffs below, not a rewritten response model.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
using String=std::string;
constexpr size_t ASYNC_RESPONCE_BUFF_SIZE=16,CONFIG_LWIP_TCP_WND_DEFAULT=64,CONFIG_LWIP_TCP_MSS=32;
constexpr size_t RESPONSE_TRY_AGAIN=static_cast<size_t>(-1);
enum {RESPONSE_SETUP,RESPONSE_HEADERS,RESPONSE_CONTENT,RESPONSE_WAIT_ACK,RESPONSE_END,RESPONSE_FAILED};
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Client {
  size_t room=64,sends=0;bool alive=true;
  String wire,pending;std::deque<size_t> limits;std::deque<bool> sendResults;
  size_t space(){return room;}
  size_t add(const char *data,size_t count){
    if(!alive)return 0;
    size_t accepted=std::min(count,room);
    if(!limits.empty()){accepted=std::min(accepted,limits.front());limits.pop_front();}
    pending.append(data,accepted);room-=accepted;return accepted;
  }
  bool send(){
    ++sends;bool sent=true;
    if(!sendResults.empty()){sent=sendResults.front();sendResults.pop_front();}
    if(sent){wire+=pending;pending.clear();}return sent;
  }
  void close(){alive=false;}
};
struct AsyncWebServerRequest {Client transport;Client *client(){return &transport;}};
class AsyncAbstractResponse {
public:
  String _assembled_headers="HEAD\n",source;
  size_t _writtenHeadersLength=0,_contentLength=0,_sentLength=0,_writtenLength=0,_ackedLength=0;
  size_t _send_buffer_offset=0,_send_buffer_len=0,_in_flight=0,_in_flight_credit=2;
  bool _source_ended=false,_chunked=false,_sendContentLength=true,valid=true;
  int _state=RESPONSE_HEADERS;
  size_t position=0,fills=0,notReady=0;
  std::unique_ptr<std::array<uint8_t,ASYNC_RESPONCE_BUFF_SIZE>> _send_buffer;
  bool _sourceValid()const{return valid;}
  size_t _fillBufferAndProcessTemplates(uint8_t *data,size_t count){
    ++fills;if(notReady){--notReady;return RESPONSE_TRY_AGAIN;}
    const auto n=std::min(count,source.size()-position);
    std::memcpy(data,source.data()+position,n);position+=n;return n;
  }
  size_t write_send_buffs(AsyncWebServerRequest*,size_t,uint32_t);
};
// ACTUAL_IMPLEMENTATION

String unchunk(const String &wire){
  size_t at=5;String body;
  for(;;){check(at+6<=wire.size(),"truncated chunk header");
    check(wire.substr(at+4,2)=="\r\n","chunk header framing");
    const auto count=std::stoul(wire.substr(at,4),nullptr,16);at+=6;
    check(at+count+2<=wire.size(),"truncated chunk body");body+=wire.substr(at,count);at+=count;
    check(wire.substr(at,2)=="\r\n","chunk tail framing");at+=2;
    if(!count){check(at==wire.size(),"duplicate chunk terminator or trailing bytes");return body;}
  }
}
void finish(AsyncAbstractResponse &r,AsyncWebServerRequest &q){
  for(unsigned budget=0;budget<200000;++budget){
    check(q.transport.alive,"response closed while healthy");
    check(r._state!=RESPONSE_FAILED,"healthy response failed");
    if(r._state==RESPONSE_END){
      check(q.transport.pending.empty(),"finished with unsent TCP data");
      check(r._ackedLength>=q.transport.wire.size(),"finished before actual admitted bytes ACK");return;
    }
    const auto outstanding=q.transport.wire.size()-r._ackedLength;
    // Real ACK callbacks can be smaller than the last admitted tail.
    const auto ack=std::min<size_t>(outstanding,3);q.transport.room=64;
    r.write_send_buffs(&q,ack,0);
  }
  throw std::runtime_error("response failed to progress under ACK/poll");
}
void content(const String &body,bool chunked,bool known){
  std::cout<<"case bytes="<<body.size()<<" chunked="<<chunked<<" known="<<known<<'\n';
  AsyncWebServerRequest q;AsyncAbstractResponse r;
  r.source=body;r._contentLength=known?body.size():0;r._chunked=chunked;r._sendContentLength=known;
  // Both final-buffer short writes and zero-admission polls occur while the
  // reported space remains larger than the accepted write (memory pressure).
  q.transport.limits={5,1,0,2,1,0,4};r.write_send_buffs(&q,0,0);
  check(r._state!=RESPONSE_END,"source EOF was mistaken for drained TCP output");
  finish(r,q);
  check(r._writtenLength==q.transport.wire.size(),"TCP accounting misses partial add or double-counts headers");
  check(q.transport.wire.substr(0,5)=="HEAD\n","header changed");
  if(!(chunked?unchunk(q.transport.wire)==body:q.transport.wire=="HEAD\n"+body))
    throw std::runtime_error("byte-exact body lost or duplicated: wire="+std::to_string(q.transport.wire.size())+" wanted="+std::to_string(body.size()+5));
}
int main(){try{
  // Baseline fails here deterministically: source filled8, TCP admitted1,
  // RESPONSE_END is exposed while7 bytes remain in the local send buffer.
  content("12345678",false,true);
  content("",false,true);
  content(String(97,'x'),false,true);
  content(String(224*1024,'p'),false,true);
  content("unknown length body",false,false);
  content("",true,false);content(String(97,'c'),true,false);
  { AsyncWebServerRequest q;AsyncAbstractResponse r;r.source="body";r._contentLength=4;
    q.transport.limits={0,0,5,0,4};r.write_send_buffs(&q,0,0);
    check(q.transport.sends==0&&q.transport.alive,"zero header add tried send or closed");
    finish(r,q);check(q.transport.wire=="HEAD\nbody","zero header retry changed bytes"); }
  { AsyncWebServerRequest q;AsyncAbstractResponse r;r.source="window";r._contentLength=6;q.transport.room=0;
    r.write_send_buffs(&q,0,0);check(q.transport.alive&&q.transport.wire.empty()&&q.transport.sends==0,"zero TCP window closed or sent");
    finish(r,q);check(q.transport.wire=="HEAD\nwindow","zero TCP window lost body"); }
  { AsyncWebServerRequest q;AsyncAbstractResponse r;r.source="ready later";r._sendContentLength=false;r.notReady=3;
    r.write_send_buffs(&q,0,0);finish(r,q);check(q.transport.wire=="HEAD\nready later","TRY_AGAIN exhausted credits"); }
  { AsyncWebServerRequest q;AsyncAbstractResponse r;r.source="1234";r._contentLength=4;
    r.write_send_buffs(&q,0,0);check(r._state==RESPONSE_WAIT_ACK,"did not wait for final ACK");
    const auto admitted=q.transport.wire.size();r.write_send_buffs(&q,admitted-1,0);
    check(r._state==RESPONSE_WAIT_ACK,"early partial ACK completed response");
    r.write_send_buffs(&q,0,0);check(r._state==RESPONSE_WAIT_ACK,"poll completed unacknowledged tail");
    r.write_send_buffs(&q,1,0);check(r._state==RESPONSE_END,"final actual ACK did not complete"); }
  { AsyncWebServerRequest q;AsyncAbstractResponse r;r.source="1234";r._contentLength=4;q.transport.sendResults={false};
    r.write_send_buffs(&q,0,0);check(r._state==RESPONSE_FAILED&&!q.transport.alive,"failed send masqueraded as success"); }
  { AsyncWebServerRequest q;AsyncAbstractResponse r;q.transport.limits={1};q.transport.sendResults={false};
    r.write_send_buffs(&q,0,0);check(r._state==RESPONSE_FAILED&&!q.transport.alive,"partial header send failure not explicit"); }
  { AsyncWebServerRequest q;AsyncAbstractResponse r;r.valid=false;
    r.write_send_buffs(&q,0,0);check(r._state==RESPONSE_FAILED&&!q.transport.alive&&q.transport.wire.empty(),"invalid source was transmitted"); }
  std::cout<<"Actual extracted response: partial/zero header+body, final ACK, empty/224KiB/unknown/chunked, TRY_AGAIN, send failure and invalid source pass\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
