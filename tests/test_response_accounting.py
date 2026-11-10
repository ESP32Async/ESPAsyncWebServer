"""Partial TCP acceptance must update return values and in-flight accounting."""
import unittest
from response_harness import method, run_cases

SOURCE = '#include "response_fixture.h"\n' + method("WebResponses.cpp", "size_t AsyncAbstractResponse::write_send_buffs") + r"""
int main(int argc, char **argv) {
  assert(argc == 2);
  const size_t quota = std::stoul(argv[1]);
  const std::string expected = "0123456789abcdefghij";
  AsyncWebServerRequest request;
  AsyncAbstractResponse response;
  response._contentLength = response._sentLength = expected.size();
  response._send_buffer = std::make_unique<std::array<uint8_t, ASYNC_RESPONCE_BUFF_SIZE>>();
  memcpy(response._send_buffer->data(), expected.data(), expected.size());
  response._send_buffer_len = expected.size();
  // space() advertises a full window, but add() can accept less (including 0).
  request.tcp.quota = quota;
  size_t ack = 0;
  for (unsigned attempt = 0; attempt < 32 && response._state != RESPONSE_END; ++attempt) {
    const size_t before = request.tcp.wire.size();
    const size_t returned = response.write_send_buffs(&request, ack, 0);
    const size_t accepted = request.tcp.wire.size() - before;
    assert(returned == accepted);
    assert(response._writtenLength == request.tcp.wire.size());
#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
    assert(response._in_flight == accepted);
#endif
    if (response._state != RESPONSE_END) {
      assert(response._send_buffer);
      assert(response._send_buffer_offset == request.tcp.wire.size());
    }
    ack = accepted;
    request.tcp.quota = std::max<size_t>(1, quota);
  }
  assert(response._state == RESPONSE_END);
  assert(!response._send_buffer && !request.tcp.closed);
  assert(request.tcp.wire == expected);
}
"""


class ResponseAccountingTests(unittest.TestCase):
    def test_partial_and_zero_acceptance(self):
        for enabled in (0, 1):
            run_cases(self, SOURCE, ("0", "1", "7", "19", "20"),
                      [f"-DASYNCWEBSERVER_USE_CHUNK_INFLIGHT={enabled}"])


if __name__ == "__main__":
    unittest.main()
