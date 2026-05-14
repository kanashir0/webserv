# Épico 06 — CGI (Common Gateway Interface)

> **Donos:** Membro 1 (execução: fork/pipe/exec) + Membro 3 (env vars + parse de output). Colaboração obrigatória conforme `docs/architecture.md`.
> **Branches:** `feat/core-network` (M1) e `feat/http-logic` (M3) — integração via PR coordenado.
> **Valor entregue:** servidor capaz de executar scripts dinâmicos (Python, PHP) integrando-se ao `EventLoop` de forma **não-bloqueante**, sem `waitpid` síncrono. É o componente mais complexo do projeto e o mais cobrado em defenses.
> **Critério de "épico pronto":** `tests/configs/cgi.conf` permite GET e POST a scripts Python; `tests/cgi/post_echo.py` retorna body ecoado; `tests/cgi/env_dump.py` mostra variáveis CGI corretas; siege com 20 conexões CGI simultâneas não trava o servidor.

---

## E06-T01 — Implementar `CgiEnv::build` (variáveis RFC 3875)

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/cgi/CgiEnv.cpp`, `include/cgi/CgiEnv.hpp`
- **Dependências:** E03-T02, E03-T03 (precisa de `Request` populado)
- **Descrição:** popular `entries_` com strings `"KEY=VALUE"` cobrindo todas as variáveis CGI/1.1 obrigatórias e os headers HTTP convertidos para `HTTP_*`.
- **Critérios de aceite:**
  - [ ] Variáveis obrigatórias: `REQUEST_METHOD`, `SCRIPT_NAME`, `PATH_INFO`, `QUERY_STRING`, `SERVER_NAME`, `SERVER_PORT`, `SERVER_PROTOCOL`, `GATEWAY_INTERFACE` (`CGI/1.1`).
  - [ ] Para POST: `CONTENT_TYPE` e `CONTENT_LENGTH` baseados nos headers da request.
  - [ ] Headers HTTP são convertidos: `User-Agent: foo` → `HTTP_USER_AGENT=foo` (caracteres `-` viram `_`, uppercased).
  - [ ] `PATH_INFO` é a porção da URI após o script (ex: `/cgi-bin/x.py/extra` → `PATH_INFO=/extra`).
  - [ ] `tests/cgi/env_dump.py` impressa as variáveis e o output bate com expected.

---

## E06-T02 — Implementar `CgiEnv::asEnvp`

- **Owner:** M3
- **Tamanho:** S
- **Arquivos afetados:** `src/cgi/CgiEnv.cpp`
- **Dependências:** E06-T01
- **Descrição:** converter `entries_` (vector de strings) para `char**` terminado em `NULL`, formato esperado por `execve()`. Os `char*` apontam para os `.c_str()` das strings em `entries_`, então a vida do `char**` é ligada à vida do `CgiEnv`.
- **Critérios de aceite:**
  - [ ] `envp_[entries_.size()] == NULL` (terminator).
  - [ ] Cada `envp_[i]` aponta para `entries_[i].c_str()`.
  - [ ] `envp_` é membro do `CgiEnv` (não local) para evitar dangling pointers.
  - [ ] `asVector()` permite testes inspecionarem o conteúdo.

---

## E06-T03 — Implementar `CgiHandler::start` (fork + pipe + execve)

- **Owner:** M1
- **Tamanho:** XL
- **Arquivos afetados:** `src/cgi/CgiHandler.cpp`, `include/cgi/CgiHandler.hpp`
- **Dependências:** E06-T01, E06-T02
- **Descrição:** o componente mais delicado do projeto:
  1. `pipe(stdin_pipe)` e `pipe(stdout_pipe)` — 4 FDs no total.
  2. `fork()`:
     - **Filho:** `dup2(stdin_pipe[0], STDIN_FILENO)`, `dup2(stdout_pipe[1], STDOUT_FILENO)`, fechar todas as outras pontas, `chdir()` para o diretório do script (CGI espera CWD do script), `execve(interpreter, args, env.asEnvp())`. Em caso de erro, `_exit(1)` (não `exit()` para evitar destrutores duplicados).
     - **Pai:** fechar pontas opostas (`stdin_pipe[0]`, `stdout_pipe[1]`), guardar `stdinPipe_` (write end) e `stdoutPipe_` (read end), aplicar `setNonBlocking` em ambos, salvar `pid_` e `startedAt_`, registrar `this` no `loop`.
- **Critérios de aceite:**
  - [ ] `fork()` é o único `fork()` em todo o projeto fora deste arquivo (`grep -rn 'fork(' src/`).
  - [ ] Filho fecha todos os FDs herdados que não são stdin/stdout (importante!).
  - [ ] Pai fecha imediatamente `stdin_pipe[0]` e `stdout_pipe[1]`.
  - [ ] Em erro de `pipe`, `fork`, ou `execve`, recursos são limpos (RAII deve cuidar).
  - [ ] Funciona com Python 3 (`/usr/bin/python3`) e PHP CGI (`/usr/bin/php-cgi`).

---

## E06-T04 — Implementar `CgiHandler::onReadable` e `onWritable`

- **Owner:** M1
- **Tamanho:** L
- **Arquivos afetados:** `src/cgi/CgiHandler.cpp`
- **Dependências:** E06-T03
- **Descrição:** integrar pipes ao Reactor. **Atenção:** `IPollable` expõe apenas 1 FD; com 2 pipes a opção mais limpa é registrar dois `IPollable` wrappers ou ajustar o `EventLoop` para suportar múltiplos FDs por pollable.
  - `onWritable()`: `write(stdinPipe_, body + offset, remaining)`. Quando body completo enviado, fechar `stdinPipe_` (script vê EOF).
  - `onReadable()`: `read(stdoutPipe_, buf, sizeof(buf))`. Acumular em `output_`. Quando retorna 0 (EOF) → `done_ = true`.
- **Critérios de aceite:**
  - [ ] Body grande (1 MB) é entregue em múltiplos `onWritable()` sem bloquear.
  - [ ] Output do script grande é acumulado em múltiplos `onReadable()`.
  - [ ] Após `done_ = true`, `wantsClose_ = true` na próxima oportunidade.
  - [ ] `EAGAIN` em qualquer pipe é tratado (apenas aguarda próximo poll).
  - [ ] Decisão sobre 2 FDs documentada inline (comentário explicando a abordagem escolhida).

---

## E06-T05 — Implementar timeout + `kill` do processo CGI

- **Owner:** M1
- **Tamanho:** M
- **Arquivos afetados:** `src/cgi/CgiHandler.cpp`, `src/core/EventLoop.cpp`
- **Dependências:** E06-T03
- **Descrição:** `timedOut(timeoutSec)` retorna `time(0) - startedAt_ > timeoutSec`. O `EventLoop::runOnce()` chama isso a cada tick e, se `true`, chama `kill(pid_, SIGKILL)` e gera resposta 500 via `makeError`. `waitpid(-1, &status, WNOHANG)` no loop colhe processos terminados sem bloquear.
- **Critérios de aceite:**
  - [ ] Script com `time.sleep(60)` e timeout de 10s é morto após 10s.
  - [ ] Timeout dispara resposta `504 Gateway Timeout` ou `500` para o cliente.
  - [ ] `waitpid(WNOHANG)` evita zombies sem bloquear o loop.
  - [ ] Default `timeoutSec = 10` configurável via `LocationConfig` (opcional).

---

## E06-T06 — Implementar `ResponseFactory::makeFromCgi` (parse de output CGI)

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** parsear o output bruto do script CGI: `<headers>\r\n\r\n<body>`. Headers do CGI podem incluir `Status: 201 Created`, `Content-Type: ...`, `Location: ...`. Mapear para `Response`: status do header `Status` (default 200 se ausente), demais headers copiados, body = tudo após `\r\n\r\n`.
- **Critérios de aceite:**
  - [ ] Output `Status: 302\r\nLocation: /foo\r\n\r\n` produz redirect 302.
  - [ ] Output sem `\r\n\r\n` (apenas headers) é tolerado (body vazio).
  - [ ] `Content-Type` do CGI é preservado.
  - [ ] Output 100% binário (PNG via CGI) é entregue sem corrupção.
  - [ ] Headers malformados → resposta 502 Bad Gateway.

---

## E06-T07 — Integrar `CgiHandler` ao `Client` (assíncrono)

- **Owner:** M1 + M3
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Client.cpp`, `src/http/handlers/PostHandler.cpp`, `src/cgi/CgiHandler.cpp`
- **Dependências:** E06-T03, E06-T04, E06-T06, E05-T03
- **Descrição:** quando o handler precisa de CGI: cria `CgiHandler`, chama `start(loop)`, sai da rota mantendo `Client` em estado `WAITING_CGI` (novo estado). O `Client` consulta `cgi.finished()` em cada tick; quando `true`, chama `takeResponse()`, monta `outBuffer_` e transita para `WRITING_RESPONSE`.
- **Critérios de aceite:**
  - [ ] Múltiplos clientes pedem CGI simultaneamente — todos são atendidos em paralelo (testar com `siege`).
  - [ ] `Client` no estado `WAITING_CGI` não monitora seu socket para POLLIN/POLLOUT (apenas mantém conexão).
  - [ ] CGI termina, resposta é enviada, conexão é liberada normalmente.
  - [ ] Cliente desconectando antes do CGI terminar → `kill(pid_)` para evitar processos órfãos.

---

## E06-T08 — Testes CGI ponta-a-ponta

- **Owner:** Todos
- **Tamanho:** M
- **Arquivos afetados:** `tests/cgi/*.py`, `tests/scripts/curl-suite.sh`
- **Dependências:** E06-T01–E06-T07
- **Descrição:** estender `curl-suite.sh` com testes para CGI: GET com query, POST com body, script que retorna binário, script que retorna 500, script que demora (verificar timeout). Adicionar 1-2 scripts PHP se interpretador estiver disponível.
- **Critérios de aceite:**
  - [ ] `curl http://localhost:8080/cgi-bin/env_dump.py?foo=bar` mostra `QUERY_STRING=foo=bar`.
  - [ ] `curl --data-binary @big.bin http://localhost:8080/cgi-bin/post_echo.py` retorna body inalterado.
  - [ ] `siege -c20 -t10s http://localhost:8080/cgi-bin/env_dump.py` sem queda nem zombies (`ps aux | grep python` zerado após teste).
  - [ ] Valgrind sem leaks após 100 chamadas CGI.

---

## Resumo de tarefas

| ID | Tarefa | Owner | Tamanho | Dependências |
|----|--------|-------|---------|-------------|
| E06-T01 | CgiEnv::build | M3 | M | E03 |
| E06-T02 | CgiEnv::asEnvp | M3 | S | T01 |
| E06-T03 | CgiHandler::start | M1 | XL | T01, T02 |
| E06-T04 | onReadable/onWritable | M1 | L | T03 |
| E06-T05 | timeout + kill | M1 | M | T03 |
| E06-T06 | makeFromCgi | M3 | M | E04-T01 |
| E06-T07 | Integração Client+CGI | M1+M3 | M | T03, T04, T06 |
| E06-T08 | Testes ponta-a-ponta | Todos | M | T01–T07 |
