#!/bin/bash

# Digest authentication anti-replay test
# Tests the digest endpoints defined in Auth.ino
#
# The library validates the signed nonce and the opaque, but does NOT check the
# header "uri" field by default (the raw request-target is not kept in memory):
# a captured "Authorization: Digest ..." header stays usable within the nonce TTL
# on any endpoint sharing the same credentials and realm. To bind a header to a
# request-target, the caller must pass the expected uri explicitly
# (see /auth-digest-bound, implemented in Auth.ino).
#
# Usage: ./test_digest.sh [SERVER_IP] [SERVER_PORT]

SERVER_IP="${1:-192.168.4.1}"
SERVER_PORT="${2:-80}"
BASE_URL="http://${SERVER_IP}:${SERVER_PORT}"
AUTH_USER="admin"
AUTH_PASS="admin"
REALM="MyApp"

echo "Testing Digest auth anti-replay protections at $BASE_URL"
echo "====================================================="

NB_PASS=0
NB_FAIL=0

ok() { echo "✅ PASS ($1)"; ((NB_PASS++)); }
ko() { echo "❌ FAIL ($1)"; ((NB_FAIL++)); }

# check(status, expected, description)
check() {
    if [ "$1" = "$2" ]; then ok "$3: $1"; else ko "$3: expected $2, got $1"; fi
}

# Portable MD5 of stdin, hex-only output (Linux: md5sum; OpenSSL CLI; macOS: BSD md5)
md5s() {
    if command -v md5sum >/dev/null 2>&1; then
        md5sum | cut -d' ' -f1
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -md5 -r | cut -d' ' -f1
    else
        md5 -q
    fi
}

# get_challenge(path): prints the last received WWW-Authenticate challenge
get_challenge() {
    curl -s -i "$BASE_URL$1" | grep -i "^WWW-Authenticate:" | tail -1 | cut -d' ' -f2- | tr -d '\r'
}

extract() {
    echo "$1" | grep -o "$2=\"[^\"]*\"" | cut -d'"' -f2
}

# build_auth(nonce, opaque, uri): prints a valid Authorization header for admin/admin
build_auth() {
    local nonce="$1" opaque="$2" uri="$3"
    local cnonce ha1 ha2 resp
    cnonce=$(printf '%s%s' "$nonce" "$opaque" | md5s | cut -c1-16)
    ha1=$(printf '%s:%s:%s' "$AUTH_USER" "$REALM" "$AUTH_PASS" | md5s)
    ha2=$(printf 'GET:%s' "$uri" | md5s)
    resp=$(printf '%s:%s:00000001:%s:auth:%s' "$ha1" "$nonce" "$cnonce" "$ha2" | md5s)
    echo "Digest username=\"$AUTH_USER\", realm=\"$REALM\", nonce=\"$nonce\", uri=\"$uri\", response=\"$resp\", cnonce=\"$cnonce\", nc=00000001, qop=auth, opaque=\"$opaque\""
}

# 1. legitimate digest auth on both endpoints (curl --digest)
STATUS=$(curl -s -o /dev/null -w "%{http_code}" --digest -u "$AUTH_USER:$AUTH_PASS" "$BASE_URL/auth-digest")
check "$STATUS" "200" "1a. legit digest auth /auth-digest"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" --digest -u "$AUTH_USER:$AUTH_PASS" "$BASE_URL/auth-digest-hash")
check "$STATUS" "200" "1b. legit digest auth /auth-digest-hash"

# 2. cross-endpoint replay: an Authorization header captured for /auth-digest
#    (replayed verbatim: response hash still computed for uri="/auth-digest")
#    is accepted by default on /auth-digest-hash, which has the same credentials
#    and realm: no raw request-target is kept, so the header uri is not checked.
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
AUTH=$(build_auth "$NONCE" "$OPAQUE" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest-hash")
check "$STATUS" "200" "2.  cross-endpoint replay accepted by default (no uri check)"

# 3. forged nonce must be rejected even with valid credentials and matching uri
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
# 40 chars, the exact length of a genuine nonce: the MAC, not the length, must reject it
FORGED="deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
AUTH=$(build_auth "$FORGED" "$OPAQUE" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest")
check "$STATUS" "401" "3.  forged nonce rejected"

# 4. foreign opaque must be rejected
AUTH=$(build_auth "$NONCE" "00000000000000000000000000000000" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest")
check "$STATUS" "401" "4.  foreign opaque rejected"

# 5. query string is NOT part of the default validation (no uri check without binding)
STATUS=$(curl -s -o /dev/null -w "%{http_code}" --digest -u "$AUTH_USER:$AUTH_PASS" "$BASE_URL/auth-digest?a=1")
check "$STATUS" "200" "5a. legit digest auth with query string"
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
AUTH=$(build_auth "$NONCE" "$OPAQUE" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest?a=1")
check "$STATUS" "200" "5b. replay with different query string accepted by default (no uri check)"

# 6. duplicated nonce must be rejected: the first occurrence (read by the stateless
#    signature/TTL validation) must not launder a second, different occurrence (over
#    which the response hash is computed). Regression test for the first-vs-last nonce
#    parser divergence that allowed replaying an expired captured header.
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
LEGIT=$(build_auth "$NONCE" "$OPAQUE" "/auth-digest")  # valid header, response computed over NONCE
FRESH_NONCE=$(extract "$(get_challenge /auth-digest)" nonce)
DUP_AUTH=${LEGIT/nonce=\"$NONCE\"/nonce=\"$FRESH_NONCE\", nonce=\"$NONCE\"}
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $DUP_AUTH" "$BASE_URL/auth-digest")
check "$STATUS" "401" "6.  duplicated nonce rejected"

# 7. uri binding enforced by the caller (RFC 7616 §3.4.6): the endpoint
#    /auth-digest-bound binds the header uri to its own path, via
#    request->authenticate(..., "/auth-digest-bound")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" --digest -u "$AUTH_USER:$AUTH_PASS" "$BASE_URL/auth-digest-bound")
check "$STATUS" "200" "7a. legit digest auth with caller-enforced uri binding"

# 8. a header issued for another endpoint must be rejected on the uri-bound endpoint
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
AUTH=$(build_auth "$NONCE" "$OPAQUE" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest-bound")
check "$STATUS" "401" "8.  cross-endpoint replay rejected with caller-enforced uri binding"

echo ""
echo "Results: $NB_PASS passed, $NB_FAIL failed"
[ "$NB_FAIL" -eq 0 ]
