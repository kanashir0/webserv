#!/usr/bin/env bash
# E08-T05 — casos-limite e malformados. Absorve E02-T08 (ConfigParser) e
# E03-T10 (RequestParser).
#
# Muitos casos o `curl` não consegue produzir — request propositalmente
# malformada, fragmentação byte a byte, pipelining. Nesses usa-se `printf | nc`
# e verifica-se a primeira linha da resposta.
#
# Uso: tests/scripts/test-edge-cases.sh

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CONF="$REPO_ROOT/conf/default.conf"
UPLOADS="$REPO_ROOT/www/uploads"
TMPDIR_="$(mktemp -d)"

cleanup() {
	stop_server
	rm -rf "$TMPDIR_"
	find "$UPLOADS" -type f ! -name '.gitkeep' -delete 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$UPLOADS"

# =========================================================================
# Parte 1 — ConfigParser (E02-T08). Sem servidor no ar: só exit code e stderr.
# =========================================================================

echo "--- config: arquivos válidos sobem ---"

# Cada .conf do repositório precisa subir e responder a um curl de sanidade.
for conf in "$REPO_ROOT"/conf/default.conf "$REPO_ROOT"/tests/configs/*.conf; do
	name="$(basename "$conf")"
	# multi-server.conf tem seu próprio script; aqui só interessa que sobe.
	HOST="localhost:8080" start_server "$conf" >/dev/null 2>&1 \
		&& { echo "${C_PASS}PASS${C_OFF}  sobe e responde: $name"; PASS=$((PASS+1)); } \
		|| { echo "${C_FAIL}FAIL${C_OFF}  sobe e responde: $name"; FAIL=$((FAIL+1)); }
	stop_server
done

echo "--- config: arquivos inválidos falham com linha ---"

# Cada caso: nome | conteúdo | linha esperada no erro | trecho da mensagem.
# A linha só chega ao usuário por causa de E02-T07; sem ela, estes casos passam
# pelo exit code mas falham na asserção da mensagem.
bad_config() {
	local name="$1" body="$2" line="$3" msg="$4"
	local f="$TMPDIR_/bad.conf" out rc
	printf '%b' "$body" > "$f"
	out="$("$REPO_ROOT/webserv" "$f" 2>&1)"; rc=$?

	if [ "$rc" = "0" ]; then
		echo "${C_FAIL}FAIL${C_OFF}  config inválida: $name (exit 0, devia falhar)"; FAIL=$((FAIL+1))
		return
	fi
	if echo "$out" | grep -q ":$line: .*$msg"; then
		echo "${C_PASS}PASS${C_OFF}  config inválida: $name (linha $line)"; PASS=$((PASS+1))
	else
		echo "${C_FAIL}FAIL${C_OFF}  config inválida: $name (esperava ':$line: ...$msg', got '$out')"; FAIL=$((FAIL+1))
	fi
}

bad_config "porta fora de faixa" \
	'server {\n  listen 99999;\n}\n' 2 'port out of range'
bad_config "diretiva desconhecida" \
	'server {\n  listen 8080;\n  foobar baz;\n}\n' 3 "unknown directive 'foobar'"
# A última diretiva antes do '}' é o caso que dispara "missing ';'": sem o
# fechamento, readDirectiveArgs esbarra no '}'. Faltando o ';' no meio do bloco
# o erro é de aridade, porque a diretiva seguinte vira argumento desta.
bad_config "';' faltando" \
	'server {\n  listen 8080;\n  root ./www\n}\n' 4 "missing ';'"
bad_config "';' faltando no meio do bloco" \
	'server {\n  listen 8080\n  root ./www;\n}\n' 3 'wrong number of arguments'
bad_config "'}' faltando" \
	'server {\n  listen 8080;\n' 3 'unexpected EOF'
bad_config "método não suportado" \
	'server {\n  listen 8080;\n  location / {\n    methods PUT;\n  }\n}\n' 4 "unsupported method 'PUT'"
bad_config "autoindex com valor inválido" \
	'server {\n  listen 8080;\n  location / {\n    autoindex talvez;\n  }\n}\n' 4 "autoindex expects 'on' or 'off'"
# Os três abaixo são o BUG-02-01, fechado no PR fix/config-semantics.
bad_config "return com código não-redirect" \
	'server {\n  listen 8080;\n  location / {\n    return 404 /x;\n  }\n}\n' 4 'return expects code 301 or 302'
bad_config "upload_store inexistente" \
	'server {\n  listen 8080;\n  location / {\n    upload_store ./nao-existe;\n  }\n}\n' 4 'not an existing directory'
bad_config "sem nenhum bloco server" \
	'# apenas um comentario\n' 2 'no server block defined'

# =========================================================================
# Parte 2 — RequestParser (E03-T10). Servidor no ar, requests cruas.
# =========================================================================

start_server "$CONF" || exit 1

echo "--- request line e headers malformados ---"

check "método com espaço → 400"        "HTTP/1.1 400 Bad Request" \
	raw_first_line 'GE T / HTTP/1.1\r\nHost: l\r\n\r\n'
check "HTTP/2.0 → 505"                 "HTTP/1.1 505 HTTP Version Not Supported" \
	raw_first_line 'GET / HTTP/2.0\r\nHost: l\r\n\r\n'
check "HTTP/1.1 sem Host → 400"        "HTTP/1.1 400 Bad Request" \
	raw_first_line 'GET / HTTP/1.1\r\n\r\n'
check "URI sem '/' inicial → 400"      "HTTP/1.1 400 Bad Request" \
	raw_first_line 'GET nao-absoluto HTTP/1.1\r\nHost: l\r\n\r\n'
check "header sem ':' → 400"           "HTTP/1.1 400 Bad Request" \
	raw_first_line 'GET / HTTP/1.1\r\nHost: l\r\nLixoSemDoisPontos\r\n\r\n'
check "header com byte nulo → 400"     "HTTP/1.1 400 Bad Request" \
	raw_first_line 'GET / HTTP/1.1\r\nHost: l\r\nX-Ruim\0: v\r\n\r\n'
check "só LF, sem CR → sem resposta"   "" \
	raw_first_line 'GET / HTTP/1.0\nHost: l\n\n'

echo "--- corpo: Content-Length e Transfer-Encoding ---"

check "POST sem CL nem TE → 411"       "HTTP/1.1 411 Length Required" \
	raw_first_line 'POST /upload/x HTTP/1.1\r\nHost: l\r\n\r\n'
check "Content-Length duplicado → 400" "HTTP/1.1 400 Bad Request" \
	raw_first_line 'POST /upload/x HTTP/1.1\r\nHost: l\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc'
check "CL + TE na mesma request → 400" "HTTP/1.1 400 Bad Request" \
	raw_first_line 'POST /upload/x HTTP/1.1\r\nHost: l\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nabc'
check "TE diferente de chunked → 400"  "HTTP/1.1 400 Bad Request" \
	raw_first_line 'POST /upload/x HTTP/1.1\r\nHost: l\r\nTransfer-Encoding: gzip\r\n\r\n'
# RFC 7230 §3.3.2: Content-Length = 1*DIGIT. O strtol aceitaria o sinal.
check "Content-Length com sinal → 400" "HTTP/1.1 400 Bad Request" \
	raw_first_line 'POST /upload/x HTTP/1.1\r\nHost: l\r\nContent-Length: +3\r\n\r\nabc'
check "Content-Length não numérico → 400" "HTTP/1.1 400 Bad Request" \
	raw_first_line 'POST /upload/x HTTP/1.1\r\nHost: l\r\nContent-Length: 12abc\r\n\r\n'

echo "--- limites ---"

# Rejeitado só pelo Content-Length, sem precisar transmitir o body.
check "body acima do limite → 413"     "HTTP/1.1 413 Payload Too Large" \
	raw_first_line 'POST /upload/x HTTP/1.1\r\nHost: l\r\nContent-Length: 99999999\r\n\r\n'
check "URI > 8192 → 414"               "414" \
	http_status "http://$HOST/$(head -c 9000 /dev/zero | tr '\0' 'a')"
check "chunked válido → 201"           "201" \
	http_status -X POST -H "Transfer-Encoding: chunked" --data-binary "abc" "http://$HOST/upload/ec.txt"

echo "--- segurança ---"

check "path traversal → 403"           "403" \
	http_status --path-as-is "http://$HOST/../../etc/passwd"
check "traversal codificado não vaza"  "403" \
	http_status --path-as-is "http://$HOST/uploads/../../../etc/passwd"

echo "--- robustez de transporte ---"

# Fragmentação: a mesma request byte a byte tem que dar a mesma resposta que
# de uma vez só. É o caso que mais compensa a ausência de teste unitário.
check "request fragmentada byte a byte" "HTTP/1.1 200 OK" \
	send_fragmented $'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'

# Conexão cortada no meio da request: o servidor não pode travar nem cair.
printf 'POST /upload/x HTTP/1.1\r\nHost: l\r\nContent-Length: 100\r\n\r\nabc' \
	| timeout 1 nc "${HOST%:*}" "${HOST#*:}" >/dev/null 2>&1
check "servidor sobrevive a conexão cortada" "200" http_status "http://$HOST/"

check "keep-alive: 2 requests, 1 conexão" "2" \
	count_responses 'GET / HTTP/1.1\r\nHost: l\r\n\r\n' 'GET / HTTP/1.1\r\nHost: l\r\n\r\n'

# BLOQUEADO POR: BUG-01-09 / PR #34 — Client::onWritable chama parser_.reset()
# no keep-alive, descartando os bytes residuais que RequestParser::take()
# preserva de propósito. O parser suporta pipelining; a camada de cima joga
# fora. O PR #34 traz o tryConsumeResidual que corrige isto.
check "pipelining: 2 requests num envio [BUG-01-09/PR#34]" "2" \
	count_responses_pipelined 'GET / HTTP/1.1\r\nHost: l\r\n\r\nGET / HTTP/1.1\r\nHost: l\r\n\r\n'

# BLOQUEADO POR: BUG-02-04 — o client_max_body_size de 10m do location /upload
# nunca é lido; vale sempre o 1m do server. Quando o Client resolver a location
# antes do body, este POST de 2 MB passa a ser aceito.
BIG="$TMPDIR_/big.bin"; head -c 2000000 /dev/zero | tr '\0' 'x' > "$BIG"
check "client_max_body_size por location [BUG-02-04]" "201" \
	http_status --data-binary @"$BIG" "http://$HOST/upload/override.bin"

# BLOQUEADO POR: BUG-01-06 — o EventLoop passa `60` literal para checkTimeout e
# o critério "timeout configurável" segue aberto. Este caso valida só que uma
# conexão ociosa recebe 408 em vez de ficar pendurada; com o valor default de
# 60s ele leva ~1min, então roda apenas com RUN_SLOW=1.
if [ "${RUN_SLOW:-0}" = "1" ]; then
	check "conexão ociosa → 408 [BUG-01-06]" "HTTP/1.1 408 Request Timeout" \
		idle_connection
fi

summary
