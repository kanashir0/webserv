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
	rm -f "$UPLOADS/$PERM_FILE" "$UPLOADS/big.bin" "$UPLOADS/toobig.bin"
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
check "CGI erro 502"     "502" curl_status "http://$HOST/cgi-bin/exit.py"
# O script nunca termina: quem desiste e o servidor, com o timeout de CGI
# (10s). O curl precisa esperar mais que isso para ver a resposta.
check "CGI travado 504"  "504" sh -c \
	"curl -s -o /dev/null --max-time 20 -w '%{http_code}' 'http://$HOST/cgi-bin/loop.py'"

# --- redirect e autoindex ----------------------------------------------
check "redirect 301"     "301" curl_status "http://$HOST/old"
check "redirect segue"   "200" curl_status -L "http://$HOST/old"
check "autoindex"        "200" curl_status "http://$HOST/files/"
# Sem autoindex e sem index file o diretorio nao pode ser listado.
check "sem autoindex"    "403" curl_status "http://$HOST/errors/"

# --- virtual host -------------------------------------------------------
check "vhost padrao"     "200" curl_status "http://$HOST/"
check "vhost site-b"     "200" curl_status -H "Host: site-b.local" "http://$HOST/"
check "vhost site-b body" "Site B" \
	sh -c "curl -s --max-time 10 -H 'Host: site-b.local' 'http://$HOST/' | grep -o 'Site B' | head -1"

# --- limite de body -----------------------------------------------------
# O server declara 1m; a location /upload sobe para 10m. Os dois valores
# precisam valer no lugar certo.
BIG2M="$(mktemp)"
BIG11M="$(mktemp)"
head -c 2097152  /dev/zero | tr '\0' 'A' > "$BIG2M"
head -c 11534336 /dev/zero | tr '\0' 'A' > "$BIG11M"

check "413 no limite server" "413" curl_status -X POST --data-binary "@$BIG2M" "http://$HOST/"
check "201 no limite loc"    "201" curl_status -X POST --data-binary "@$BIG2M" \
                                       "http://$HOST/upload/big.bin"
check "413 acima do loc"     "413" curl_status -X POST --data-binary "@$BIG11M" \
                                       "http://$HOST/upload/toobig.bin"
check "GET do upload"        "200" curl_status "http://$HOST/upload/big.bin"
check "DELETE do upload"     "204" curl_status -X DELETE "http://$HOST/upload/big.bin"
rm -f "$BIG2M" "$BIG11M"

# --- requisicoes malformadas (nao podem derrubar o servidor) ------------
check "request invalida"  "400" sh -c \
	"printf 'GARBAGE\r\n\r\n' | timeout 3 nc ${HOST%%:*} ${HOST##*:} | head -1 | cut -d' ' -f2"
check "versao invalida"   "505" sh -c \
	"printf 'GET / HTTP/9.9\r\nHost: x\r\n\r\n' | timeout 3 nc ${HOST%%:*} ${HOST##*:} | head -1 | cut -d' ' -f2"
check "sem Host"          "400" sh -c \
	"printf 'GET / HTTP/1.1\r\n\r\n' | timeout 3 nc ${HOST%%:*} ${HOST##*:} | head -1 | cut -d' ' -f2"
check "POST sem length"   "411" sh -c \
	"printf 'POST /upload/x HTTP/1.1\r\nHost: x\r\n\r\n' | timeout 3 nc ${HOST%%:*} ${HOST##*:} | head -1 | cut -d' ' -f2"
check "servidor vivo"     "200" curl_status "http://$HOST/"

# --- sessoes e cookies (bonus) -----------------------------------------
JAR="$(mktemp)"
check "sessao visita 1"  "1" sh -c \
	"curl -s --max-time 10 -c '$JAR' 'http://$HOST/session' | grep -oE 'visit number <strong>[0-9]+' | grep -oE '[0-9]+'"
check "sessao visita 2"  "2" sh -c \
	"curl -s --max-time 10 -b '$JAR' 'http://$HOST/session' | grep -oE 'visit number <strong>[0-9]+' | grep -oE '[0-9]+'"
check "sessao sem cookie" "1" sh -c \
	"curl -s --max-time 10 'http://$HOST/session' | grep -oE 'visit number <strong>[0-9]+' | grep -oE '[0-9]+'"
rm -f "$JAR"

echo ""
echo "passed=$PASS failed=$FAIL"
[ "$FAIL" = "0" ]
