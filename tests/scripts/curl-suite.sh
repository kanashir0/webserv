#!/usr/bin/env bash
# E08-T01 — smoke tests do caminho feliz: status, headers, bodies, ciclo
# POST→GET→DELETE, virtual hosting e CGI. Absorve E04-T09 e E05-T06.
#
# Sobe e derruba o servidor sozinho com conf/default.conf.
# Uso: tests/scripts/curl-suite.sh

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CONF="$REPO_ROOT/conf/default.conf"
UPLOADS="$REPO_ROOT/www/uploads"

cleanup() {
	stop_server
	find "$UPLOADS" -type f ! -name '.gitkeep' -delete 2>/dev/null
}
trap cleanup EXIT

# Idempotência: o diretório precisa existir e estar vazio antes e depois, senão
# um DELETE pode passar por causa do lixo da rodada anterior (BUG-08-02).
mkdir -p "$UPLOADS"
find "$UPLOADS" -type f ! -name '.gitkeep' -delete 2>/dev/null

start_server "$CONF" || exit 1

# --- status codes ---------------------------------------------------------

check "GET / → 200"                  "200" http_status "http://$HOST/"
check "GET inexistente → 404"        "404" http_status "http://$HOST/__nope__"
check "GET dir sem index → 403"      "403" http_status "http://$HOST/errors/"
check "PUT → 405"                    "405" http_status -X PUT "http://$HOST/"
# O caso antigo esperava 403 aqui. conf/default.conf declara `location / { methods GET; }`,
# então 405 com Allow: GET é o correto — o teste é que estava errado (BUG-08-01).
check "DELETE em location GET → 405" "405" http_status -X DELETE "http://$HOST/"
check "redirect → 301"               "301" http_status "http://$HOST/old"

# --- headers, não só status ----------------------------------------------

check "405 traz Allow"               "GET"  http_header Allow -X PUT "http://$HOST/"
check "301 traz Location"            "/"    http_header Location "http://$HOST/old"
check "Content-Type por extensão"    "text/html" http_header Content-Type "http://$HOST/errors/404.html"
check "404 serve a error_page"       "404"  http_status "http://$HOST/__nope__"
check_match "body do 404 é o do error_page" "404" http_body "http://$HOST/__nope__"

# --- ciclo POST → GET → DELETE → GET (absorve E05-T06) --------------------

# O upload cai em www/uploads/, então o GET é por /uploads/ — que o
# longest-prefix match resolve para o mesmo `location /upload` (prefixo puro).
POST_URL="http://$HOST/upload/ciclo.txt"
GET_URL="http://$HOST/uploads/ciclo.txt"
check "POST → 201"                "201"               http_status --data-binary "conteudo-do-ciclo" "$POST_URL"
check "201 traz Content-Location" "/upload/cl.txt"   http_header Content-Location --data-binary "x" "http://$HOST/upload/cl.txt"
check "GET do arquivo criado"     "conteudo-do-ciclo" http_body "$GET_URL"
check "DELETE → 204"              "204"               http_status -X DELETE "$POST_URL"
check "GET após DELETE → 404"     "404"               http_status "$GET_URL"

# --- limites e encodings --------------------------------------------------

check "URI > 8192 → 414" "414" http_status "http://$HOST/$(head -c 9000 /dev/zero | tr '\0' 'a')"

# 2 MB contra o client_max_body_size de 1m do server.
# NOTA: o /upload declara `client_max_body_size 10m`, que deveria permitir este
# body — mas o override por location nunca é lido (BUG-02-04), então vale o
# limite do server. Quando o M1 corrigir, este caso vira 201.
BIG="$(mktemp)"; head -c 2000000 /dev/zero | tr '\0' 'x' > "$BIG"
check "body acima do limite → 413" "413" http_status --data-binary @"$BIG" "http://$HOST/upload/big.bin"
rm -f "$BIG"

check "POST chunked → 201" "201" http_status -X POST -H "Transfer-Encoding: chunked" \
	--data-binary "abc" "http://$HOST/upload/chunked.txt"

# --- virtual hosting ------------------------------------------------------

check "Host conhecido → 200"     "200" http_status -H "Host: localhost"    "http://$HOST/"
check "Host desconhecido → default" "200" http_status -H "Host: nada.local" "http://$HOST/"
# Roteamento entre vhosts distintos é o test-multi-server.sh.

# --- CGI ------------------------------------------------------------------
# BLOQUEADO POR: BUG-05-01 (crítico, M3/E06-T09) — hoje um GET a .py devolve o
# código-fonte do script com 200. Estes três falham até o CGI ser implementado.

check_no_match "GET .py não vaza código-fonte [BUG-05-01]" "import sys|#!/usr/bin" \
	http_body "http://$HOST/cgi-bin/hello.py"

# Não basta procurar "Hello from CGI" no body: essa string está no código-fonte
# do script, então ela passa mesmo com o BUG-05-01 ativo. O que só existe se o
# CGI de fato rodou é o Content-Type que o script emite no próprio header.
check "GET CGI roda o script [BUG-05-01]" "text/plain" \
	http_header Content-Type "http://$HOST/cgi-bin/hello.py"

check_match "GET CGI recebe QUERY_STRING [BUG-05-01]" "QUERY_STRING=a=1&b=2" \
	http_body "http://$HOST/cgi-test/env_dump.py?a=1&b=2"
check_match "POST CGI recebe o body no stdin [E05-T03]" "Received 3 bytes" \
	http_body --data-binary "abc" "http://$HOST/cgi-test/post_echo.py"

summary
