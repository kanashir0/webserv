# Testing — Webserv

## Smoke (rápido)

```bash
make
./webserv conf/default.conf &
tests/scripts/curl-suite.sh
```

## Stress

```bash
tests/scripts/run-siege.sh
# Esperado: Availability >= 99.5%, sem queda do binário.
```

## Memory leaks

```bash
tests/scripts/run-valgrind.sh conf/default.conf
# Esperado: 0 bytes lost, 0 FDs leaked.
```

## Edge cases manuais

| Cenário                       | Esperado |
|-------------------------------|----------|
| `curl http://localhost:8080/` | 200 com index.html |
| `curl http://localhost:8080/__nope__` | 404 |
| `curl -X PUT http://localhost:8080/` | 405 |
| `curl --data-binary @big.bin http://localhost:8080/upload` (acima do limite) | 413 |
| URI com 9000 chars            | 414 |
| Body com `Transfer-Encoding: chunked` | 200 |
| Body sem `Content-Length` em POST | 411 |

## Browsers

Chrome e Firefox em `http://localhost:8080/` — index renderiza, devtools sem erro.
