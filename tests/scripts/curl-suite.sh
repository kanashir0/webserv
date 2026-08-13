#!/usr/bin/env bash
# Suite minima de smoke tests por curl. Cada teste imprime PASS/FAIL.
#
# Uso:
#   tests/scripts/curl-suite.sh              # sobe ./webserv com conf/default.conf
#   tests/scripts/curl-suite.sh host:port    # usa um servidor ja em execucao
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HOST="${1:-}"
UPLOADS="$ROOT/www/uploads"
PERM_FILE="perm-test.txt"
SERVER_PID=""
PASS=0
FAIL=0

# O teste de 403 mexe na permissao do diretorio: guardar o modo exato para
# devolver depois (um `chmod u+w` cru perderia o bit de grupo).
UPLOADS_MODE="$(stat -c '%a' "$UPLOADS" 2>/dev/null)"

restore_uploads() {
	if [ -n "$UPLOADS_MODE" ]; then
		chmod "$UPLOADS_MODE" "$UPLOADS" 2>/dev/null
	else
		chmod u+w "$UPLOADS" 2>/dev/null
	fi
}

cleanup() {
	# Restaura mesmo se a suite for interrompida no meio.
	restore_uploads
	rm -f "$UPLOADS/$PERM_FILE"
	if [ -n "$SERVER_PID" ]; then
		kill "$SERVER_PID" 2>/dev/null
		wait "$SERVER_PID" 2>/dev/null
	fi
}
trap cleanup EXIT

start_server() {
	"$ROOT/webserv" "$ROOT/conf/default.conf" > /dev/null 2>&1 &
	SERVER_PID=$!
	# Espera a porta aceitar conexao em vez de dormir um tempo fixo.
	local i=0
	while [ "$i" -lt 50 ]; do
		if curl -s -o /dev/null --max-time 1 "http://$HOST/" 2>/dev/null; then
			return 0
		fi
		if ! kill -0 "$SERVER_PID" 2>/dev/null; then
			echo "erro: o servidor morreu ao iniciar"
			return 1
		fi
		sleep 0.1
		i=$((i+1))
	done
	echo "erro: o servidor nao respondeu em 5s"
	return 1
}

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
	curl -s -o /dev/null --max-time 10 -w "%{http_code}" "$@"
}

curl_body() {
	curl -s --max-time 10 "$@"
}

if [ -z "$HOST" ]; then
	HOST="127.0.0.1:8080"
	start_server || exit 1
fi

# --- estatico -----------------------------------------------------------
check "GET /"            "200" curl_status "http://$HOST/"
check "GET 404"          "404" curl_status "http://$HOST/__nope__"

# location / declara `methods GET`: DELETE e PUT sao metodo nao permitido.
check "DELETE 405"       "405" curl_status -X DELETE "http://$HOST/"
check "method invalid"   "405" curl_status -X PUT    "http://$HOST/"

# --- upload -------------------------------------------------------------
check "POST upload"      "201" curl_status -X POST --data-binary "webserv" \
                                   "http://$HOST/upload/$PERM_FILE"

# 403 real: o arquivo existe, mas o diretorio-pai nao e gravavel.
chmod a-w "$UPLOADS"
check "DELETE forbidden" "403" curl_status -X DELETE "http://$HOST/upload/$PERM_FILE"
restore_uploads

check "DELETE upload"    "204" curl_status -X DELETE "http://$HOST/upload/$PERM_FILE"

# --- cgi ----------------------------------------------------------------
check "CGI GET"          "200" curl_status "http://$HOST/cgi-bin/hello.py"
# Garante que o script foi executado, e nao servido como arquivo estatico.
check "CGI executa"      "Hello from CGI!" curl_body "http://$HOST/cgi-bin/hello.py"
check "CGI POST"         "200" curl_status -X POST -d "campo=valor" \
                                   "http://$HOST/cgi-bin/hello.py"
check "CGI 404"          "404" curl_status "http://$HOST/cgi-bin/__nope__.py"
check "CGI falha 502"    "502" curl_status "http://$HOST/cgi-bin/broken.py"

echo ""
echo "passed=$PASS failed=$FAIL"
[ "$FAIL" = "0" ]
