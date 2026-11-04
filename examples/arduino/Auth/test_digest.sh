#!/bin/bash

# Digest authentication replay protection test
# Tests the digest endpoints defined in Auth.ino
#
# Usage: ./test_digest.sh [SERVER_IP]

SERVER_IP="${1:-192.168.4.1}"
SERVER_PORT="${2:-80}"
BASE_URL="http://${SERVER_IP}:${SERVER_PORT}"
AUTH_USER="admin"
AUTH_PASS="admin"

echo "Testing Digest auth replay protections at $BASE_URL"
echo "====================================================="

NB_PASS=0
NB_FAIL=0

ok() { echo "✅ PASS ($1)"; ((NB_PASS++)); }
ko() { echo "❌ FAIL ($1)"; ((NB_FAIL++)); }

# check(status, expected, description)
check() {
    if [ "$1" = "$2" ]; then ok "$3: $1"; else ko "$3: expected $2, got $1"; fi
}

# get_challenge(path): prints "nonce|opaque" of the last received challenge
get_challenge() {
    curl -s -i "$BASE_URL$1" | grep -i "^WWW-Authenticate:" | cut -d' ' -f2- | tr -d '\r'
}

extract() {
    echo "$1" | grep -o "$2=\"[^\"]*\"" | cut -d'"' -f2
}

# build_auth(nonce, opaque, uri): prints a valid Authorization header for admin/admin
build_auth() {
    local nonce="$1" opaque="$2" uri="$3"
    local cnonce ha1 ha2 resp
    cnonce=$(echo "$nonce$opaque" | md5 | cut -c1-16)
    ha1=$(echo -n "$AUTH_USER:MyApp:$AUTH_PASS" | md5 | cut -d' ' -f1)
    ha2=$(echo -n "GET:$uri" | md5 | cut -d' ' -f1)
    resp=$(echo -n "$ha1:$nonce:00000001:$cnonce:auth:$ha2" | md5 | cut -d' ' -f1)
    echo "Digest username=\"$AUTH_USER\", realm=\"MyApp\", nonce=\"$nonce\", uri=\"$uri\", response=\"$resp\", cnonce=\"$cnonce\", nc=00000001, qop=auth, opaque=\"$opaque\""
}

# 1. legitimate digest auth on both endpoints (curl --digest)
STATUS=$(curl -s -o /dev/null -w "%{http_code}" --digest -u ""$AUTH_USER:$AUTH_PASS"" "$BASE_URL/auth-digest")
check "$STATUS" "200" "1a. legit digest auth /auth-digest"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" --digest -u ""$AUTH_USER:$AUTH_PASS"" "$BASE_URL/auth-digest-hash")
check "$STATUS" "200" "1b. legit digest auth /auth-digest-hash"

# 2. cross-endpoint replay: an Authorization header captured for /auth-digest
#    (replayed verbatim: response hash still computed for uri="/auth-digest")
#    must be rejected on /auth-digest-hash, which has the same credentials and realm.
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
AUTH=$(build_auth "$NONCE" "$OPAQUE" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest-hash")
check "$STATUS" "401" "2.  cross-endpoint verbatim replay rejected"

# 3. forged nonce must be rejected even with valid credentials and matching uri
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
FORGED="deadbeef00000000000000000000000000000000000000000000000000000000"
AUTH=$(build_auth "$FORGED" "$OPAQUE" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest")
check "$STATUS" "401" "3.  forged nonce rejected"

# 4. foreign opaque must be rejected
AUTH=$(build_auth "$NONCE" "00000000000000000000000000000000" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest")
check "$STATUS" "401" "4.  foreign opaque rejected"

# 5. query string is part of the uri binding
STATUS=$(curl -s -o /dev/null -w "%{http_code}" --digest -u ""$AUTH_USER:$AUTH_PASS"" "$BASE_URL/auth-digest?a=1")
check "$STATUS" "200" "5a. legit digest auth with query string"
CH=$(get_challenge /auth-digest)
NONCE=$(extract "$CH" nonce)
OPAQUE=$(extract "$CH" opaque)
AUTH=$(build_auth "$NONCE" "$OPAQUE" "/auth-digest")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $AUTH" "$BASE_URL/auth-digest?a=1")
check "$STATUS" "401" "5b. replay with different query string rejected"

echo ""
echo "Results: $NB_PASS passed, $NB_FAIL failed"
[ "$NB_FAIL" -eq 0 ]