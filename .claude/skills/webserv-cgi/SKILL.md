---
name: webserv-cgi
description: Guia de integração CGI (RFC 3875) que cruza M1 (fork/pipes/poll), M2 (parser de config cgi e headers de saída) e M3 (preparação de env e roteamento). Use ao implementar ou debugar fluxo de scripts .py/.php.
---

# webserv-cgi — integração CGI ponta a ponta

CGI é o componente mais complexo do projeto e o único que cruza os 3 donos.
Implemente em par/trio, não isoladamente.

## Divisão de trabalho

| Etapa | Dono | Saída |
|-------|------|-------|
| Detectar extensão CGI na config (`cgi .py /usr/bin/python3`) | M2 | `LocationConfig::cgiInterpreters` (`map<string,string>`) |
| Router decide "isso é CGI" e chama `CgiHandler` | M3 | dispatch |
| Preparar env (`CgiEnv`) seguindo RFC 3875 | M3 | `vector<string>` de `KEY=VAL` |
| `fork()` + `dup2()` + `execve()` + pipes não-bloqueantes | M1 | `CgiHandler` rodando, FDs no `EventLoop` |
| Streaming de body do POST para stdin do filho | M1 | `onWritable()` no pipe |
| Coleta de stdout do filho até EOF | M1 | buffer acumulado |
| Parse da saída CGI (headers + body) → `Response` | M3 | `ResponseFactory::makeFromCgi` |

## Variáveis de ambiente (RFC 3875 mínimo)

Todas obrigatórias:

```
GATEWAY_INTERFACE=CGI/1.1
SERVER_PROTOCOL=HTTP/1.1
SERVER_SOFTWARE=webserv/1.0
SERVER_NAME=<vhost server_name ou IP>
SERVER_PORT=<porta do listening>
REQUEST_METHOD=<GET|POST|DELETE>
PATH_INFO=<porção da URI após o script>
PATH_TRANSLATED=<root + PATH_INFO>
SCRIPT_NAME=<URI até o script, sem query>
SCRIPT_FILENAME=<path absoluto do .py>
QUERY_STRING=<após '?' na URI; vazio se ausente>
REMOTE_ADDR=<IP do cliente>
CONTENT_LENGTH=<bytes do body; vazio em GET>
CONTENT_TYPE=<header Content-Type da request>
REDIRECT_STATUS=200          # PHP-CGI exige isso
```

Headers HTTP arbitrários viram `HTTP_<NAME_UPPER>`:
- `Host: foo` → `HTTP_HOST=foo`
- `User-Agent: curl` → `HTTP_USER_AGENT=curl`

## Fluxo do CgiHandler (M1)

```
                     ┌──────────────┐
                     │   Client     │
                     │ (POST /x.py) │
                     └──────┬───────┘
                            │ body
                ┌───────────▼────────────┐
                │      pipe in[2]        │
                │ pai → in[1] → in[0] → 0│
                └────────────────────────┘
                            │
                       fork() exec
                            │
                ┌───────────▼────────────┐
                │      pipe out[2]       │
                │ 1 → out[1] → out[0] → pai
                └───────────┬────────────┘
                            │ stdout do script
                     ┌──────▼───────┐
                     │ ResponseFact │
                     │   makeFromCgi│
                     └──────────────┘
```

Passos no pai (após fork):

```cpp
close(in[0]);  close(out[1]);                    // fecha pontas do filho
fcntl(in[1],  F_SETFL, O_NONBLOCK);
fcntl(out[0], F_SETFL, O_NONBLOCK);
loop.add(this);                                  // CgiHandler é IPollable
// interest() retorna POLLOUT até esgotar body, depois POLLIN para ler stdout
```

No filho (antes do execve):

```cpp
dup2(in[0], 0);    close(in[0]); close(in[1]);
dup2(out[1], 1);   close(out[0]); close(out[1]);
// fechar TODOS os outros FDs herdados (clientes, listening, etc)
for (int i = 3; i < 1024; ++i) close(i);
chdir(scriptDir);                                // CGI espera CWD = dir do script
execve(interpreter, argv, envp);
_exit(1);                                        // execve falhou
```

## Parse da saída do CGI

Formato (RFC 3875):

```
Status: 200 OK\r\n          (opcional; default 200)
Content-Type: text/html\r\n  (obrigatório se há body)
<outros headers>\r\n
\r\n
<body bytes>
```

`makeFromCgi`:
1. Encontra `\r\n\r\n` (ou `\n\n`).
2. Tudo antes vira headers; `Status:` é especial → vira status code.
3. Tudo depois vira body.
4. Se não veio `Content-Type`: erro 502 Bad Gateway.

## Timeout e cleanup

- Timeout configurável (default 10s). Após expirar: `kill(pid, SIGKILL)` +
  `waitpid(pid, &status, 0)` (já morto, retorna imediato).
- Sempre `waitpid(WNOHANG)` no `onHangup()` para não deixar zumbi.
- Se script escreve mais que o pipe buffer e ninguém lê: deadlock. Por isso
  o pipe out **tem que** estar no `poll()` desde o início.

## Erros comuns

| Sintoma | Causa |
|---------|-------|
| Script roda mas resposta vem em branco | Esqueceu `\r\n\r\n` no script ou `Content-Type` |
| Servidor trava no POST grande | Pipe in não está no poll → buffer cheio bloqueia |
| Defunct/zombie processes | Falta `waitpid` |
| Script vê env vazio | `execve` ignora `environ`; passe `envp` explicitamente |
| 502 sempre | Geralmente parse de headers do CGI quebrado |
| Vaza FD ao morrer cedo | `~CgiHandler` deve fechar pipes restantes via `FileDescriptor` |

## Teste mínimo

`cgi-bin/hello.py`:

```python
#!/usr/bin/env python3
import os, sys
print("Content-Type: text/plain\r")
print("\r")
print(f"Hello from CGI. Method={os.environ.get('REQUEST_METHOD')}")
print(f"Query={os.environ.get('QUERY_STRING')}")
body = sys.stdin.read()
print(f"Body bytes={len(body)}")
```

```bash
chmod +x cgi-bin/hello.py
curl "http://localhost:8080/cgi-bin/hello.py?x=42"
curl -d "ola=mundo" "http://localhost:8080/cgi-bin/hello.py"
```
