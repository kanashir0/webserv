---
name: webserv-test
description: Roda a bateria completa de testes do webserv (curl-suite, siege stress, valgrind com --track-fds, edge cases manuais do subject). Use após qualquer mudança que toque rede, parser ou handlers.
---

# webserv-test — bateria de testes

Sempre rode os 4 níveis na ordem. Pare no primeiro que falhar.

## 1. Smoke (curl-suite)

```bash
make
./webserv conf/default.conf &
SERVER_PID=$!
sleep 1
tests/scripts/curl-suite.sh
RESULT=$?
kill $SERVER_PID 2>/dev/null
wait 2>/dev/null
test $RESULT -eq 0 && echo "smoke: OK" || echo "smoke: FAIL"
```

## 2. Edge cases manuais (todos os 7 do subject)

Servidor rodando em background:

| # | Comando | Esperado |
|---|---------|----------|
| 1 | `curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/` | 200 |
| 2 | `curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/__nope__` | 404 |
| 3 | `curl -s -o /dev/null -w "%{http_code}" -X PUT http://localhost:8080/` | 405 |
| 4 | `head -c 20M /dev/urandom > /tmp/big.bin && curl -s -o /dev/null -w "%{http_code}" --data-binary @/tmp/big.bin http://localhost:8080/upload` | 413 |
| 5 | `curl -s -o /dev/null -w "%{http_code}" "http://localhost:8080/$(printf 'a%.0s' {1..9000})"` | 414 |
| 6 | `printf 'POST /upload HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n' \| nc -q1 localhost 8080` | 200/201 |
| 7 | `printf 'POST /upload HTTP/1.1\r\nHost: localhost\r\n\r\nhello' \| nc -q1 localhost 8080` | 411 |

Cada falha é bug no módulo correspondente:
- 1/2 → routing (M3)
- 3 → method validation (M3)
- 4 → `client_max_body_size` (M2 config + M1/M2 parser)
- 5 → URI length cap (M2 parser)
- 6 → chunked decoding (M2 parser)
- 7 → `Content-Length` obrigatório em POST (M2 parser)

## 3. Stress (siege)

```bash
which siege || { echo "siege não instalado: sudo apt install siege"; exit 1; }
./webserv conf/default.conf &
SERVER_PID=$!
sleep 1
tests/scripts/run-siege.sh | tee /tmp/siege.log
kill $SERVER_PID
```

**Critérios:**
- `Availability` ≥ **99.5 %**
- `Failed transactions` baixo (subject permite < 1 %)
- Servidor **não cai** (checar com `kill -0 $SERVER_PID` antes do kill)
- Sem mensagens de "leak" ou crash em stderr

## 4. Valgrind (memory + FDs)

```bash
tests/scripts/run-valgrind.sh conf/default.conf 2>&1 | tee /tmp/valgrind.log
```

Em outra aba, dispare requisições típicas (`tests/scripts/curl-suite.sh`),
depois `Ctrl+C` no servidor e analise:

```bash
grep -E 'definitely lost|indirectly lost|FILE DESCRIPTORS' /tmp/valgrind.log
```

**Critérios obrigatórios:**
- `definitely lost: 0 bytes in 0 blocks`
- `indirectly lost: 0 bytes in 0 blocks`
- FDs abertos no fim: apenas 0,1,2 (stdin/stdout/stderr) — qualquer outro
  é leak de FD e reprova.

## Browsers (manual)

Abra Chrome e Firefox em `http://localhost:8080/`. Verifique:
- Index renderiza
- DevTools → Network → sem requests pendentes ou erros
- Refresh repetido (Ctrl+Shift+R) não trava o servidor

## Relatório final

Reporte tabela:

```
Smoke    : OK / FAIL
Edge 1-7 : 7/7 passando
Siege    : Availability 99.X %
Valgrind : 0 bytes lost, 0 FDs leaked
```

Se algo falhou, indique o módulo provavelmente responsável.
