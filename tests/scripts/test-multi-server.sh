#!/usr/bin/env bash
# E08-T06 — virtual hosting: dois server{} na mesma porta distinguidos pelo
# header Host, e um terceiro em outra porta.
#
# Os asserts olham o body (grep pelo marcador VHOST-A/VHOST-B), não o status —
# um 200 sozinho não prova que o vhost certo respondeu.
#
# Uso: tests/scripts/test-multi-server.sh

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CONF="$REPO_ROOT/tests/configs/multi-server.conf"

trap stop_server EXIT

start_server "$CONF" || exit 1

vhost_body() { http_body -H "Host: $1" "http://$HOST/"; }

echo "--- roteamento por Host na mesma porta ---"

check_match "site-a.local → vhost A" "VHOST-A" vhost_body "site-a.local"
check_match "site-b.local → vhost B" "VHOST-B" vhost_body "site-b.local"
# Sem isto, um dos dois testes acima passaria por acidente se ambos caíssem no
# mesmo vhost default.
check_no_match "site-b.local não serve o vhost A" "VHOST-A" vhost_body "site-b.local"

echo "--- fallback e normalização do Host ---"

check_match "Host desconhecido → primeiro declarado" "VHOST-A" vhost_body "nao-existe.local"

# BLOQUEADO POR: BUG-01-05 / PR #34 — matchVirtualHost não remove a porta do
# header Host, então o Host que TODO browser manda nunca casa com o server_name
# e cai sempre no vhost default.
check_match "Host com porta [BUG-01-05/PR#34]" "VHOST-B" vhost_body "site-b.local:8080"

# BLOQUEADO POR: BUG-01-05 / PR #34 — a comparação é ==, não iequals. Nomes de
# host são case-insensitive (RFC 7230 §5.4).
check_match "Host em caixa alta [BUG-01-05/PR#34]" "VHOST-B" vhost_body "SITE-B.LOCAL"

echo "--- listeners independentes ---"

check "outra porta responde" "200" http_status -H "Host: outra-porta.local" "http://localhost:8081/"

echo "--- startup ---"

# BLOQUEADO POR: BUG-01-04 / PR #34 — Server::start agrupa os vhosts errado e
# tenta um bind redundante na porta já ocupada pelo primeiro server{}.
check_no_match "startup sem BIND FALHOU [BUG-01-04/PR#34]" "BIND FALHOU|FALHA EM BINDAR" \
	cat /tmp/webserv-test.log

summary
