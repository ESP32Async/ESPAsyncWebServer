// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#pragma once

#include "Arduino.h"

// Outcome of digest verification when server-side checks (nonce/opaque/uri) are applied.
enum class DigestAuthStatus {
  DIGEST_AUTH_FAILED,       // credentials or header rejected
  DIGEST_AUTH_SUCCESS,      // fully verified
  DIGEST_AUTH_STALE_NONCE,  // nonce was issued by this server but has expired: re-challenge with stale=true
};

// Full verification: parses the Digest header, checks credentials, and — using the
// stateless scheme — validates the server-issued signed nonce and the instance opaque.
// No per-nonce storage is kept. now is expected to be a monotonic millisecond counter (e.g. millis()).
// nonce, opaque and uri are caller overrides: a non-NULL value means the caller takes that
// specific check over. With a NULL uri (default) the header uri is left unchecked: the library
// does not keep the raw request-target; callers willing to bind the header to a resource
// (RFC 7616 section 3.4.6) must pass the expected effective request URI.
DigestAuthStatus checkDigestAuthentication(
  const char *header, const char *method, const char *username, const char *password, const char *realm, bool passwordIsHash, const uint8_t digestSecret[16],
  uint32_t digestNonceTtl, const char *nonce = nullptr, const char *opaque = nullptr, const char *uri = nullptr
);

// Extracts one parameter value from a Digest header (e.g. getDigestParam(header, "nonce", out)).
// Quote-aware: values in quoted form are returned without their quotes.
// Returns false when the parameter is absent; an empty value yields an empty out string.
bool getDigestParam(const char *header, const char *name, String &out);

// Stateless signed nonce: hex(millis) + hex(HMAC-MD5(secret, hex(millis))) : 40 chars.
// Validity is re-computed from the embedded timestamp, without any server-side storage.
String generateDigestNonce(const uint8_t secret[16], uint32_t now);

// Opaque value derived from the instance secret, identifying this server instance.
String generateDigestOpaque(const uint8_t secret[16]);

// Validates a signed nonce. Returns false when the nonce was not issued from this
// secret. When true and the nonce has expired, stale is set: the caller should
// re-challenge with stale=true (RFC 7616 section 3.3) so compliant clients silently re-authenticate.
bool validateDigestNonce(const char *nonce, const uint8_t secret[16], uint32_t now, uint32_t ttl, bool &stale);

// HMAC-MD5 (RFC 2104)
void hmacMD5(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t out[16]);

// Fill a 16-byte secret with best available platform entropy.
void generateSecret(uint8_t secret[16]);

bool checkBasicAuthentication(const char *header, const char *username, const char *password);

bool checkDigestAuthentication(
  const char *header, const char *method, const char *username, const char *password, const char *realm, bool passwordIsHash, const char *nonce,
  const char *opaque, const char *uri
);

// for storing hashed versions on the device that can be authenticated against
String generateDigestHash(const char *username, const char *password, const char *realm);

String generateBasicHash(const char *username, const char *password);

String genRandomMD5();
