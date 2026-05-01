#!/usr/bin/env bash
# Suíte mínima de smoke tests por curl. Cada teste imprime PASS/FAIL.
# Uso: tests/scripts/curl-suite.sh [host:port]
set -u

HOST="${1:-localhost:8080}"
PASS=0
FAIL=0

check() {
	local name="$1"; shift
	local expected="$1"; shift
	local got
	got="$("$@")"
	if [ "$got" = "$expected" ]; then
		echo "PASS  $name ($expected)"
		PASS=$((PASS+1))
	else
		echo "FAIL  $name (expected=$expected got=$got)"
		FAIL=$((FAIL+1))
	fi
}

curl_status() {
	curl -s -o /dev/null -w "%{http_code}" "$@"
}

check "GET /"          "200" curl_status "http://$HOST/"
check "GET 404"        "404" curl_status "http://$HOST/__nope__"
check "DELETE forbid"  "403" curl_status -X DELETE "http://$HOST/"
check "method invalid" "405" curl_status -X PUT    "http://$HOST/"

echo ""
echo "passed=$PASS failed=$FAIL"
[ "$FAIL" = "0" ]
