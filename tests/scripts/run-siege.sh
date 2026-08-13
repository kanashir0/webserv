#!/usr/bin/env bash
# Stress test com siege. O subject exige disponibilidade > 99.5%.
#
# Uso:
#   tests/scripts/run-siege.sh              # sobe o servidor e testa
#   tests/scripts/run-siege.sh host:port    # usa um servidor ja em execucao
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HOST="${1:-}"
URLS="$ROOT/.siege-urls.txt"
SERVER_PID=""

if ! command -v siege > /dev/null; then
	echo "erro: siege nao instalado. Rode: sudo apt install -y siege"
	exit 1
fi

cleanup() {
	rm -f "$URLS"
	if [ -n "$SERVER_PID" ]; then
		kill "$SERVER_PID" 2>/dev/null
		wait "$SERVER_PID" 2>/dev/null
	fi
}
trap cleanup EXIT

if [ -z "$HOST" ]; then
	HOST="127.0.0.1:8080"
	"$ROOT/webserv" "$ROOT/conf/default.conf" > /dev/null 2>&1 &
	SERVER_PID=$!
	for _ in $(seq 1 50); do
		curl -s -o /dev/null --max-time 1 "http://$HOST/" 2>/dev/null && break
		kill -0 "$SERVER_PID" 2>/dev/null || { echo "erro: o servidor morreu ao iniciar"; exit 1; }
		sleep 0.1
	done
fi

# Mistura estatico, 404 e CGI: so bater em / nao exercita o fork do CGI, que e
# onde um vazamento de FD ou processo apareceria.
cat > "$URLS" <<EOF
http://$HOST/
http://$HOST/__nope__
http://$HOST/cgi-bin/hello.py
EOF

echo ">>> siege: 50 clientes, 30s, misturando estatico e CGI"
siege -b -c 50 -t 30s -f "$URLS" 2>&1 | tee /tmp/siege-out.$$ | grep -Ei "Transactions|Availability|Failed|Longest"

AVAIL="$(grep -i "Availability" /tmp/siege-out.$$ | grep -oE "[0-9]+\.[0-9]+" | head -1)"
rm -f /tmp/siege-out.$$

echo ""
if [ -n "$AVAIL" ] && [ "$(echo "$AVAIL > 99.5" | bc -l 2>/dev/null)" = "1" ]; then
	echo "OK: disponibilidade ${AVAIL}% (subject exige > 99.5%)"
else
	echo "ATENCAO: disponibilidade ${AVAIL:-desconhecida}% — abaixo do exigido"
	exit 1
fi
