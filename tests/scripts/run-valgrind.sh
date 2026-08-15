#!/usr/bin/env bash
# Roda o webserv sob valgrind, aplica carga e reporta leaks de memoria e de FD.
#
# Uso: tests/scripts/run-valgrind.sh [arquivo.conf]
#
# --child-silent-after-fork e essencial aqui: cada requisicao CGI faz fork(), e
# sem essa flag o valgrind emite um relatorio para cada processo filho, enchendo
# a saida de falsos positivos que nao sao do servidor.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CONF="${1:-$ROOT/conf/default.conf}"
HOST="127.0.0.1:8080"
LOG="$ROOT/valgrind.log"
VG_PID=""

if ! command -v valgrind > /dev/null; then
	echo "erro: valgrind nao instalado. Rode: sudo apt install -y valgrind"
	exit 1
fi

cleanup() {
	if [ -n "$VG_PID" ] && kill -0 "$VG_PID" 2>/dev/null; then
		# SIGINT: o webserv trata e sai pelo caminho normal, deixando o valgrind
		# escrever o relatorio final. SIGKILL perderia o relatorio.
		kill -INT "$VG_PID" 2>/dev/null
		for _ in $(seq 1 50); do
			kill -0 "$VG_PID" 2>/dev/null || break
			sleep 0.2
		done
	fi
}
trap cleanup EXIT

echo ">>> subindo webserv sob valgrind (config: $CONF)"
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-fds=yes \
         --trace-children=no \
         --child-silent-after-fork=yes \
         --log-file="$LOG" \
         "$ROOT/webserv" "$CONF" > /dev/null 2>&1 &
VG_PID=$!

for _ in $(seq 1 100); do
	curl -s -o /dev/null --max-time 1 "http://$HOST/" 2>/dev/null && break
	kill -0 "$VG_PID" 2>/dev/null || { echo "erro: o servidor morreu ao iniciar; veja $LOG"; exit 1; }
	sleep 0.2
done

echo ">>> aplicando carga (estatico, upload, erros e CGI)"
curl -s -o /dev/null                                  "http://$HOST/"
curl -s -o /dev/null                                  "http://$HOST/__nope__"
curl -s -o /dev/null -X DELETE                        "http://$HOST/"
curl -s -o /dev/null -X PUT                           "http://$HOST/"
curl -s -o /dev/null -X POST --data-binary "webserv"  "http://$HOST/upload/vg.txt"
curl -s -o /dev/null -X DELETE                        "http://$HOST/upload/vg.txt"
curl -s -o /dev/null                                  "http://$HOST/cgi-bin/hello.py"
curl -s -o /dev/null -X POST -d "a=1"                 "http://$HOST/cgi-bin/hello.py"
curl -s -o /dev/null                                  "http://$HOST/cgi-bin/broken.py"
curl -s -o /dev/null                                  "http://$HOST/cgi-bin/__nope__.py"
# Duas requisicoes na mesma conexao: exercita keep-alive e o buffer residual.
curl -s -o /dev/null "http://$HOST/" -o /dev/null "http://$HOST/__nope__"
# Body em chunks: exercita o caminho de unchunk do parser.
curl -s -o /dev/null -X POST -H "Transfer-Encoding: chunked" \
     --data-binary "webserv" "http://$HOST/upload/vg-chunked.txt"
curl -s -o /dev/null -X DELETE "http://$HOST/upload/vg-chunked.txt"
# Body acima do limite: exercita o caminho de 413.
head -c 2000000 /dev/zero | tr '\0' 'x' | curl -s -o /dev/null -X POST --data-binary @- "http://$HOST/"
# Requisicoes malformadas: exercitam os caminhos de erro do parser.
for BAD in 'LIXO\r\n\r\n' 'GET / HTTP/2.0\r\nHost: x\r\n\r\n' 'GET / HTTP/1.1\r\n\r\n'; do
	printf "$BAD" | timeout 3 nc "${HOST%%:*}" "${HOST##*:}" > /dev/null 2>&1 || true
done

echo ">>> encerrando para o valgrind escrever o relatorio"
cleanup
VG_PID=""
sleep 1

echo ""
echo "================= RESULTADO ================="
grep -E "total heap usage|in use at exit|All heap blocks|definitely lost|indirectly lost|possibly lost" "$LOG" \
	| sed 's/^==[0-9]*== */  /'
echo ""
grep -E "FILE DESCRIPTORS" "$LOG" | sed 's/^==[0-9]*== */  /'
# O proprio --log-file conta como um FD aberto; qualquer outro nao-std e do servidor.
STRAY="$(grep "^==[0-9]*== Open file descriptor" "$LOG" | grep -vc "$LOG")"
echo "  FDs abertos que nao sao std nem o log do valgrind: $STRAY"
echo ""
grep -E "ERROR SUMMARY" "$LOG" | sed 's/^==[0-9]*== */  /'
echo "============================================="
echo "relatorio completo em: $LOG"
echo ""

# Sem vazamento nenhum o valgrind imprime "All heap blocks were freed" e omite
# as linhas "definitely lost"; por isso as duas formas contam como sucesso.
OK=1
if ! grep -qE "All heap blocks were freed|definitely lost: 0 bytes" "$LOG"; then
	echo "FALHOU: ha vazamento de memoria."
	OK=0
fi
if ! grep -q "ERROR SUMMARY: 0 errors" "$LOG"; then
	echo "FALHOU: valgrind reportou erros de memoria."
	OK=0
fi
if [ "$STRAY" != "0" ]; then
	echo "FALHOU: o servidor deixou $STRAY file descriptor(s) aberto(s)."
	OK=0
fi

if [ "$OK" = "1" ]; then
	echo "OK: zero leaks, zero erros de memoria, zero FDs vazados."
else
	exit 1
fi
