#!/usr/bin/env bash
# Infra compartilhada das três suítes. Não é executável: use `source`.
#
# Casos bloqueados por bug de outro membro entram como teste normal, com o ID do
# bug no nome — eles FALHAM de propósito. A suíte é o sinal de quando o bug
# fechou, não um enfeite; por isso não há flag de skip.

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${HOST:-localhost:8080}"
PASS=0
FAIL=0
SRV_PID=""

if [ -t 1 ]; then
	C_PASS=$'\033[32m'; C_FAIL=$'\033[31m'; C_OFF=$'\033[0m'
else
	C_PASS=""; C_FAIL=""; C_OFF=""
fi

# --- servidor -------------------------------------------------------------

start_server() {
	local conf="$1"
	"$REPO_ROOT/webserv" "$conf" >/tmp/webserv-test.log 2>&1 &
	SRV_PID=$!
	# Espera o socket aceitar em vez de dormir um valor fixo.
	local i=0
	while [ $i -lt 50 ]; do
		if curl -s -o /dev/null --max-time 1 "http://$HOST/" 2>/dev/null; then return 0; fi
		kill -0 "$SRV_PID" 2>/dev/null || { echo "servidor morreu ao subir com '$conf':"; cat /tmp/webserv-test.log; return 1; }
		sleep 0.1; i=$((i+1))
	done
	echo "servidor não respondeu em 5s com '$conf'"; return 1
}

stop_server() {
	[ -n "$SRV_PID" ] || return 0
	kill "$SRV_PID" 2>/dev/null
	wait "$SRV_PID" 2>/dev/null
	SRV_PID=""
}

# --- asserts --------------------------------------------------------------

check() {
	local name="$1" expected="$2"; shift 2
	local got; got="$("$@")"
	if [ "$got" = "$expected" ]; then
		echo "${C_PASS}PASS${C_OFF}  $name ($expected)"; PASS=$((PASS+1))
	else
		echo "${C_FAIL}FAIL${C_OFF}  $name (expected=$expected got=$got)"; FAIL=$((FAIL+1))
	fi
}

# Passa se a saída do comando casa com o regex.
check_match() {
	local name="$1" pattern="$2"; shift 2
	local got; got="$("$@")"
	if echo "$got" | grep -qE "$pattern"; then
		echo "${C_PASS}PASS${C_OFF}  $name (~ $pattern)"; PASS=$((PASS+1))
	else
		echo "${C_FAIL}FAIL${C_OFF}  $name (esperava ~ '$pattern', got='$(echo "$got" | head -1)')"; FAIL=$((FAIL+1))
	fi
}

# Passa se a saída do comando NÃO casa com o regex.
check_no_match() {
	local name="$1" pattern="$2"; shift 2
	local got; got="$("$@")"
	if echo "$got" | grep -qE "$pattern"; then
		echo "${C_FAIL}FAIL${C_OFF}  $name (não podia conter '$pattern')"; FAIL=$((FAIL+1))
	else
		echo "${C_PASS}PASS${C_OFF}  $name (!~ $pattern)"; PASS=$((PASS+1))
	fi
}

# --- helpers de HTTP ------------------------------------------------------

http_status()  { curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$@"; }
http_headers() { curl -s -o /dev/null -D - --max-time 5 "$@"; }
http_body()    { curl -s --max-time 5 "$@"; }

# Valor de um header da resposta, sem CR final.
http_header() {
	local name="$1"; shift
	http_headers "$@" | grep -i "^$name:" | head -1 | cut -d' ' -f2- | tr -d '\r'
}

# Request crua: só o que `curl` não consegue produzir (malformada, pipelining).
raw_send() { printf "%b" "$1" | timeout 5 nc -q1 "${HOST%:*}" "${HOST#*:}" 2>/dev/null; }
raw_first_line() { raw_send "$1" | head -1 | tr -d '\r'; }

# N requests na MESMA conexão, com pausa entre elas: keep-alive de verdade.
# Conta as linhas de status que voltaram.
count_responses() {
	local req
	{ for req in "$@"; do printf "%b" "$req"; sleep 0.3; done; sleep 0.5; } \
		| timeout 10 nc -q1 "${HOST%:*}" "${HOST#*:}" 2>/dev/null | grep -c "^HTTP/1.1 "
}

# Tudo num único envio: pipelining. Conta as respostas.
count_responses_pipelined() { raw_send "$1" | grep -c "^HTTP/1.1 "; }

# Envia a request byte a byte. Prova que o parser lida com qualquer
# granularidade de recv() — o `fold`/`read` mangla o CR, por isso o loop de
# índice sobre a string.
send_fragmented() {
	local req="$1" i
	{
		for ((i = 0; i < ${#req}; i++)); do printf '%s' "${req:$i:1}"; sleep 0.005; done
		sleep 0.5
	} | timeout 15 nc -q1 "${HOST%:*}" "${HOST#*:}" 2>/dev/null | head -1 | tr -d '\r'
}

# Abre a conexão, manda uma request incompleta e espera o servidor desistir.
idle_connection() {
	{ printf 'GET / HTTP/1.1\r\n'; sleep 120; } \
		| timeout 130 nc "${HOST%:*}" "${HOST#*:}" 2>/dev/null | head -1 | tr -d '\r'
}

# --- resultado ------------------------------------------------------------

summary() {
	echo ""
	echo "passed=$PASS failed=$FAIL"
	[ "$FAIL" = "0" ]
}
