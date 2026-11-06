// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#include "WebAuthentication.h"
#include "AsyncWebServerLogging.h"
#include <libb64/cencode.h>

#include <cinttypes>
#include <cstdio>

#if defined(ESP32)
#include <esp_system.h>
#elif defined(ESP8266)
#include <esp8266_peri.h>
#endif

#include "./literals.h"

// platform classification for the MD5 backend used in this file:
//  - HAS_MD5_BUILDER: the MD5Builder wrapper class exists (ESP32 arduino, RP2040, HOST)
//  - everything else (ESP8266, LibreTiny): low-level md5_context_t C API from <md5.h>
#if defined(ESP32) || defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(HOST)
#define HAS_MD5_BUILDER 1
#else
#define HAS_MD5_BUILDER 0
#endif

#if HAS_MD5_BUILDER
#include <MD5Builder.h>
#else
#include <md5.h>
#endif

using namespace asyncsrv;

// Basic Auth hash = base64("username:password")

bool checkBasicAuthentication(const char *hash, const char *username, const char *password) {
  if (username == NULL || password == NULL || hash == NULL) {
    return false;
  }
  return generateBasicHash(username, password).equalsIgnoreCase(hash);
}

String generateBasicHash(const char *username, const char *password) {
  if (username == NULL || password == NULL) {
    return asyncsrv::emptyString;
  }

  size_t toencodeLen = strlen(username) + strlen(password) + 1;

  char *toencode = new char[toencodeLen + 1];
  if (toencode == NULL) {
    return asyncsrv::emptyString;
  }
  char *encoded = new char[base64_encode_expected_len(toencodeLen) + 1];
  if (encoded == NULL) {
    delete[] toencode;
    return asyncsrv::emptyString;
  }
  sprintf_P(toencode, PSTR("%s:%s"), username, password);
  if (base64_encode_chars(toencode, toencodeLen, encoded) > 0) {
    String res = String(encoded);
    delete[] toencode;
    delete[] encoded;
    return res;
  }
  delete[] toencode;
  delete[] encoded;
  return asyncsrv::emptyString;
}

static bool getMD5(uint8_t *data, uint16_t len, char *output) {  // 33 bytes or more
#if HAS_MD5_BUILDER
  MD5Builder md5;
  md5.begin();
  md5.add(data, len);
  md5.calculate();
  md5.getChars(output);
#else
  md5_context_t _ctx;

  uint8_t *_buf = (uint8_t *)malloc(16);
  if (_buf == NULL) {
    return false;
  }
  memset(_buf, 0x00, 16);

  MD5Init(&_ctx);
  MD5Update(&_ctx, data, len);
  MD5Final(_buf, &_ctx);

  for (uint8_t i = 0; i < 16; i++) {
    sprintf_P(output + (i * 2), PSTR("%02x"), _buf[i]);
  }

  free(_buf);
#endif
  return true;
}

String genRandomMD5() {
#ifdef ESP8266
  uint32_t r = RANDOM_REG32;
#else
  uint32_t r = rand();  // NOLINT(runtime/threadsafe_fn)
#endif
  char *out = (char *)malloc(33);
  if (out == NULL || !getMD5((uint8_t *)(&r), 4, out)) {
    async_ws_log_e("Failed to allocate");
    return asyncsrv::emptyString;
  }
  String res = String(out);
  free(out);
  return res;
}

void generateSecret(uint8_t secret[16]) {
#if defined(ESP32)
  for (uint8_t i = 0; i < 16; i++) {
    secret[i] = (uint8_t)esp_random();
  }
#elif defined(ESP8266)
  uint32_t r[4] = {RANDOM_REG32, RANDOM_REG32, RANDOM_REG32, RANDOM_REG32};
  memcpy(secret, r, 16);
#elif defined(LIBRETINY)
  ESP.random(secret, 16);
#else
  // HOST is POSIX: read from /dev/urandom, falling back to rand()
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) {
    size_t read = fread(secret, 1, 16, f);
    fclose(f);
    if (read == 16) {
      return;
    }
  }
  for (uint8_t i = 0; i < 16; i++) {
    secret[i] = (uint8_t)rand();
  }
#endif
}

#if HAS_MD5_BUILDER
// Adds a buffer to a MD5Builder, working around the pre-3.x core where add()
// takes a non-const uint8_t* although it never modifies the data.
static inline void md5Add(MD5Builder &md5, const uint8_t *data, size_t len) {
#if defined(ESP32) && ESP_ARDUINO_VERSION_MAJOR < 3
  md5.add(const_cast<uint8_t *>(data), len);
#else
  md5.add(data, len);
#endif
}
#endif

// HMAC-MD5, RFC 2104
void hmacMD5(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t out[16]) {
  uint8_t k0[64];
  memset(k0, 0, sizeof(k0));
  if (keyLen > 64) {
    // keys longer than block size are hashed first
#if HAS_MD5_BUILDER
    MD5Builder md5;
    md5.begin();
    md5Add(md5, key, keyLen);
    md5.calculate();
    md5.getBytes(k0);  // writes exactly 16 bytes
#else
    md5_context_t ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, key, keyLen);
    MD5Final(k0, &ctx);
#endif
  } else {
    memcpy(k0, key, keyLen);
  }

  uint8_t inner[64], outer[64];
  for (uint8_t i = 0; i < 64; i++) {
    inner[i] = k0[i] ^ 0x36;
    outer[i] = k0[i] ^ 0x5c;
  }

  uint8_t digest[16];
#if HAS_MD5_BUILDER
  MD5Builder md5;
  md5.begin();
  md5Add(md5, inner, 64);
  md5Add(md5, data, dataLen);
  md5.calculate();
  md5.getBytes(digest);  // writes exactly 16 bytes
  md5 = MD5Builder();
  md5.begin();
  md5Add(md5, outer, 64);
  md5Add(md5, digest, 16);
  md5.calculate();
  md5.getBytes(out);  // writes exactly 16 bytes
#else
  md5_context_t ctx;
  MD5Init(&ctx);
  MD5Update(&ctx, inner, 64);
  MD5Update(&ctx, data, dataLen);
  MD5Final(digest, &ctx);
  MD5Init(&ctx);
  MD5Update(&ctx, outer, 64);
  MD5Update(&ctx, digest, 16);
  MD5Final(out, &ctx);
#endif
}

// Lowercase hex encoding of a buffer (output length is 2 * len + 1).
static String toHexDigest(const uint8_t *buf, size_t len) {
  String out;
  if (!out.reserve(len * 2)) {
    return asyncsrv::emptyString;
  }
  for (size_t i = 0; i < len; i++) {
    out += T_hex_chars[buf[i] >> 4];
    out += T_hex_chars[buf[i] & 0x0f];
  }
  return out;
}

// nonce = hex(millis) + hex(HMAC-MD5(secret, hex(millis))) : 8 + 32 = 40 chars
String generateDigestNonce(const uint8_t secret[16], uint32_t now) {
  char ts[9];
  snprintf(ts, sizeof(ts), "%08" PRIx32, now);
  uint8_t mac[16];
  hmacMD5(secret, 16, (const uint8_t *)ts, 8, mac);
  String macHex = toHexDigest(mac, 16);
  if (!macHex.length()) {
    async_ws_log_e("Failed to allocate");
    return asyncsrv::emptyString;
  }
  String nonce;
  if (!nonce.reserve(40)) {
    async_ws_log_e("Failed to allocate");
    return asyncsrv::emptyString;
  }
  nonce.concat(ts, 8);
  nonce.concat(macHex);
  return nonce;
}

String generateDigestOpaque(const uint8_t secret[16]) {
  uint8_t opaqueMac[16];
  hmacMD5(secret, 16, (const uint8_t *)T_opaque, strlen(T_opaque), opaqueMac);
  String opaque = toHexDigest(opaqueMac, 16);
  if (!opaque.length()) {
    async_ws_log_e("Failed to allocate");
  }
  return opaque;
}

bool validateDigestNonce(const char *nonce, const uint8_t secret[16], uint32_t now, uint32_t ttl, bool &stale) {
  stale = false;
  if (nonce == NULL || strlen(nonce) != 40) {
    return false;
  }
  char ts[9];
  memcpy(ts, nonce, 8);
  ts[8] = '\0';
  uint32_t issued;
  if (sscanf(ts, "%08" SCNx32, &issued) != 1) {
    return false;
  }
  // recompute the MAC part and compare in constant time, without any allocation
  uint8_t mac[16];
  hmacMD5(secret, 16, (const uint8_t *)ts, 8, mac);
  const char *got = nonce + 8;
  uint8_t diff = 0;
  for (uint8_t i = 0; i < 16; i++) {
    diff |= (uint8_t)T_hex_chars[mac[i] >> 4] ^ (uint8_t)got[i * 2];
    diff |= (uint8_t)T_hex_chars[mac[i] & 0x0f] ^ (uint8_t)got[i * 2 + 1];
  }
  if (diff) {
    return false;
  }
  // modular arithmetic on the monotonic counters: safe across millis() wrap-around
  if (ttl && (uint32_t)(now - issued) > ttl) {
    stale = true;  // issued by this server (MAC valid) but expired
  }
  return true;
}

// Extracts one parameter value from a Digest header (e.g. getDigestParam(header, "nonce", out)).
// Quote-aware: values in quoted form are returned without their quotes.
// Returns false when the parameter is absent; an empty value yields an empty out string.
bool getDigestParam(const char *header, const char *name, String &out) {
  out = asyncsrv::emptyString;
  if (header == NULL || name == NULL) {
    return false;
  }
  const size_t nameLen = strlen(name);
  const char *p = header;
  const char *end = header + strlen(header);
  while (p < end) {
    // skip leading separators
    while (p < end && (*p == ',' || *p == ' ')) {
      p++;
    }
    if (p >= end) {
      return false;
    }
    // match the parameter name at this boundary, followed by '='
    if ((size_t)(end - p) > nameLen && memcmp(p, name, nameLen) == 0 && p[nameLen] == '=') {
      const char *val = p + nameLen + 1;
      if (*val == '"') {
        val++;
        const char *quote = strchr(val, '"');
        if (!quote) {
          return false;  // unterminated quoted value
        }
        return out.concat(val, quote - val);
      }
      const char *valEnd = val;
      while (valEnd < end && *valEnd != ',') {
        valEnd++;
      }
      return out.concat(val, valEnd - val);
    }
    // not this parameter: skip to the next separator
    p = strchr(p, ',');
    if (!p) {
      return false;
    }
  }
  return false;
}

static String stringMD5(const String &in) {
  char *out = (char *)malloc(33);
  if (out == NULL || !getMD5((uint8_t *)(in.c_str()), in.length(), out)) {
    async_ws_log_e("Failed to allocate");
    return asyncsrv::emptyString;
  }
  String res = String(out);
  free(out);
  return res;
}

String generateDigestHash(const char *username, const char *password, const char *realm) {
  if (username == NULL || password == NULL || realm == NULL) {
    return asyncsrv::emptyString;
  }
  char *out = (char *)malloc(33);
  if (out == NULL) {
    async_ws_log_e("Failed to allocate");
    return asyncsrv::emptyString;
  }

  String in;
  if (!in.reserve(strlen(username) + strlen(realm) + strlen(password) + 2)) {
    async_ws_log_e("Failed to allocate");
    free(out);
    return asyncsrv::emptyString;
  }

  in.concat(username);
  in.concat(':');
  in.concat(realm);
  in.concat(':');
  in.concat(password);

  if (!getMD5((uint8_t *)(in.c_str()), in.length(), out)) {
    async_ws_log_e("Failed to allocate");
    free(out);
    return asyncsrv::emptyString;
  }

  in = String(out);
  free(out);
  return in;
}

bool checkDigestAuthentication(
  const char *header, const char *method, const char *username, const char *password, const char *realm, bool passwordIsHash, const char *nonce,
  const char *opaque, const char *uri
) {
  if (username == NULL || password == NULL || header == NULL || method == NULL) {
    // os_printf("AUTH FAIL: missing required fields\n");
    return false;
  }

  String myHeader(header);
  int nextBreak = myHeader.indexOf(',');
  if (nextBreak < 0) {
    // os_printf("AUTH FAIL: no variables\n");
    return false;
  }

  String myUsername;
  String myRealm;
  String myNonce;
  String myUri;
  String myResponse;
  String myQop;
  String myNc;
  String myCnonce;

  myHeader += (char)0x2c;  // ','
  myHeader += (char)0x20;  // ' '
  do {
    String avLine(myHeader.substring(0, nextBreak));
    avLine.trim();
    myHeader = myHeader.substring(nextBreak + 1);
    nextBreak = myHeader.indexOf(',');

    int eqSign = avLine.indexOf('=');
    if (eqSign < 0) {
      // os_printf("AUTH FAIL: no = sign\n");
      return false;
    }
    String varName(avLine.substring(0, eqSign));
    avLine = avLine.substring(eqSign + 1);
    if (avLine.startsWith(String('"'))) {
      avLine = avLine.substring(1, avLine.length() - 1);
    }

    if (varName.equals(T_username)) {
      if (!avLine.equals(username)) {
        // os_printf("AUTH FAIL: username\n");
        return false;
      }
      myUsername = avLine;
    } else if (varName.equals(T_realm)) {
      if (realm != NULL && !avLine.equals(realm)) {
        // os_printf("AUTH FAIL: realm\n");
        return false;
      }
      myRealm = avLine;
    } else if (varName.equals(T_nonce)) {
      if (nonce != NULL && !avLine.equals(nonce)) {
        // os_printf("AUTH FAIL: nonce\n");
        return false;
      }
      myNonce = avLine;
    } else if (varName.equals(T_opaque)) {
      if (opaque != NULL && !avLine.equals(opaque)) {
        // os_printf("AUTH FAIL: opaque\n");
        return false;
      }
    } else if (varName.equals(T_uri)) {
      if (uri != NULL && !avLine.equals(uri)) {
        // os_printf("AUTH FAIL: uri\n");
        return false;
      }
      myUri = avLine;
    } else if (varName.equals(T_response)) {
      myResponse = avLine;
    } else if (varName.equals(T_qop)) {
      myQop = avLine;
    } else if (varName.equals(T_nc)) {
      myNc = avLine;
    } else if (varName.equals(T_cnonce)) {
      myCnonce = avLine;
    }
  } while (nextBreak > 0);

  String ha1 = passwordIsHash ? password : stringMD5(myUsername + ':' + myRealm + ':' + password).c_str();
  String ha2 = stringMD5(String(method) + ':' + myUri);
  String response = ha1 + ':' + myNonce + ':' + myNc + ':' + myCnonce + ':' + myQop + ':' + ha2;

  if (myResponse.equals(stringMD5(response))) {
    // os_printf("AUTH SUCCESS\n");
    return true;
  }

  // os_printf("AUTH FAIL: password\n");
  return false;
}

// Full digest verification with stateless server-side checks.
// The nonce embeds its issuing time and is signed with the per-server secret, so no
// per-nonce storage is required: any nonce not signed by this server, or expired, is rejected.
// nonce, opaque and uri are caller overrides: a non-NULL value means the caller takes that
// specific check over. With a NULL uri (default) the header uri is left unchecked: the library
// does not keep the raw request-target; callers willing to bind the header to a resource
// (RFC 7616 section 3.4.6) must pass the expected effective request URI.
// Fails closed: on allocation failure the request is rejected.
DigestAuthStatus checkDigestAuthentication(
  const char *header, const char *method, const char *username, const char *password, const char *realm, bool passwordIsHash, const uint8_t digestSecret[16],
  uint32_t digestNonceTtl, const char *nonce, const char *opaque, const char *uri
) {
  if (header == NULL || method == NULL || username == NULL || password == NULL || digestSecret == NULL) {
    return DigestAuthStatus::DIGEST_AUTH_FAILED;
  }

  String myNonce;
  String myOpaque;
  if (!getDigestParam(header, T_nonce, myNonce) || !getDigestParam(header, T_opaque, myOpaque)) {
    return DigestAuthStatus::DIGEST_AUTH_FAILED;  // malformed header: missing mandatory fields
  }

  // opaque must be the one bound to this server instance (compare on the stack, no allocation)
  if (myOpaque.length() != 32) {
    return DigestAuthStatus::DIGEST_AUTH_FAILED;
  }
  if (opaque != NULL) {  // caller override: compare verbatim against the expected value
    if (myOpaque != opaque) {
      async_ws_log_e("Digest auth: invalid opaque");
      return DigestAuthStatus::DIGEST_AUTH_FAILED;
    }
  } else {
    uint8_t opaqueMac[16];
    hmacMD5(digestSecret, 16, (const uint8_t *)T_opaque, strlen(T_opaque), opaqueMac);
    uint8_t diff = 0;
    const char *got = myOpaque.c_str();
    for (uint8_t i = 0; i < 16; i++) {
      diff |= (uint8_t)T_hex_chars[opaqueMac[i] >> 4] ^ (uint8_t)got[i * 2];
      diff |= (uint8_t)T_hex_chars[opaqueMac[i] & 0x0f] ^ (uint8_t)got[i * 2 + 1];
    }
    if (diff) {
      async_ws_log_e("Digest auth: invalid opaque");
      return DigestAuthStatus::DIGEST_AUTH_FAILED;
    }
  }

  // nonce must have been issued by this server, and must not be expired
  if (nonce != NULL) {  // caller override: compare verbatim against the expected value
    if (myNonce != nonce) {
      async_ws_log_e("Digest auth: invalid nonce");
      return DigestAuthStatus::DIGEST_AUTH_FAILED;
    }
  } else {
    bool stale = false;
    if (!validateDigestNonce(myNonce.c_str(), digestSecret, millis(), digestNonceTtl, stale)) {
      async_ws_log_e("Digest auth: invalid nonce");
      return DigestAuthStatus::DIGEST_AUTH_FAILED;
    }
    if (stale) {
      return DigestAuthStatus::DIGEST_AUTH_STALE_NONCE;
    }
  }

  // credentials + response hash. The validated nonce/opaque are passed to the inner check:
  // it compares every occurrence of these parameters against them, so a header carrying
  // duplicated nonce/opaque fields (e.g. a fresh one first, an expired one last, used to
  // bypass the validation with the last occurrence) is rejected. A caller-supplied uri is
  // likewise compared against every occurrence.
  if (!checkDigestAuthentication(header, method, username, password, realm, passwordIsHash, myNonce.c_str(), myOpaque.c_str(), uri)) {
    return DigestAuthStatus::DIGEST_AUTH_FAILED;
  }

  return DigestAuthStatus::DIGEST_AUTH_SUCCESS;
}
