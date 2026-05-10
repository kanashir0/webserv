# Épico 01 — Motor de Rede e Reactor Pattern

> **Dono primário:** Membro 1 (M1)
> **Branch:** `feat/core-network`
> **Valor entregue:** servidor capaz de aceitar conexões TCP, gerenciar múltiplos clientes simultâneos via `poll()` único e mover dados entre socket ↔ parser ↔ resposta sem nunca bloquear o processo.
> **Critério de "épico pronto":** `./webserv conf/default.conf` aceita conexões, mantém múltiplos clientes em paralelo e responde com mocks/handlers sem travar (`siege -c10 -t10s` sem queda).

---

## E01-T01 — Implementar `Socket::bindAndListen`

- **Owner:** M1
- **Tamanho:** S
- **Arquivos afetados:** `src/common/Socket.cpp`, `include/common/Socket.hpp`
- **Dependências:** nenhuma
- **Descrição:** implementar a sequência `socket() → setsockopt(SO_REUSEADDR) → bind() → listen(backlog=128)` na função `Socket::bindAndListen(host, port, backlog)`. Em erro de qualquer syscall, lançar `std::runtime_error` com mensagem de `errno` (via `strerror`).
- **Critérios de aceite:**
  - [ ] FD do socket é armazenado em `FileDescriptor` interno (RAII).
  - [ ] `SO_REUSEADDR` é aplicado antes do `bind()` para permitir reinicialização rápida do servidor.
  - [ ] Erros de `bind` em portas privilegiadas (<1024) e portas em uso geram exceção com mensagem clara.
  - [ ] `host == "0.0.0.0"` resulta em escuta em todas as interfaces; `host == "127.0.0.1"` apenas localhost.
  - [ ] Após `bindAndListen`, `socket_.fd() >= 0` e o servidor consegue iniciar.

---

## E01-T02 — Implementar `Socket::setNonBlocking`

- **Owner:** M1
- **Tamanho:** S
- **Arquivos afetados:** `src/common/Socket.cpp`
- **Dependências:** E01-T01
- **Descrição:** aplicar `fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)` para que `recv/send/accept` retornem imediatamente com `EAGAIN/EWOULDBLOCK` em vez de bloquear.
- **Critérios de aceite:**
  - [ ] Recebe `int fd` e aplica `O_NONBLOCK` preservando outras flags.
  - [ ] Falha em `fcntl` lança `std::runtime_error` com `errno`.
  - [ ] Após chamada, `recv` em socket vazio retorna -1 com `errno == EAGAIN`.

---

## E01-T03 — Implementar `Socket::accept`

- **Owner:** M1
- **Tamanho:** S
- **Arquivos afetados:** `src/common/Socket.cpp`
- **Dependências:** E01-T01
- **Descrição:** chamar `::accept(fd, &addr, &len)` e retornar o FD do cliente. Em `EAGAIN/EWOULDBLOCK`, retornar `-1` sem lançar exceção (é fluxo normal quando todas as conexões já foram drenadas).
- **Critérios de aceite:**
  - [ ] Preenche `outAddr` com IP/porta do cliente.
  - [ ] Retorna `-1` em `EAGAIN/EWOULDBLOCK` sem logar erro.
  - [ ] Retorna `-1` e loga `WARN` em outros erros.
  - [ ] FD retornado **não** é gerenciado pelo `Socket` (ownership transferido ao chamador).

---

## E01-T04 — Implementar `EventLoop::runOnce` (poll + dispatch)

- **Owner:** M1
- **Tamanho:** L
- **Arquivos afetados:** `src/core/EventLoop.cpp`, `include/core/EventLoop.hpp`
- **Dependências:** E01-T01, E01-T02
- **Descrição:** este é o **único** ponto de `poll()` em todo o projeto. Construir array `pollfd[]` a partir de `pollables_` (preenchendo `.fd = p->fd()` e `.events = p->interest()`), chamar `poll(fds, n, timeoutMs)`, e despachar eventos: `POLLIN → onReadable()`, `POLLOUT → onWritable()`, `POLLHUP|POLLERR → onHangup()`. Após o dispatch, chamar `reapClosed()`.
- **Critérios de aceite:**
  - [ ] Apenas 1 ocorrência de `poll(` em todo o código-fonte (`grep -rn 'poll(' src/` retorna só esta linha).
  - [ ] Despacho funciona para `ListeningSocket`, `Client` e `CgiHandler` sem código específico de tipo.
  - [ ] `EINTR` em `poll()` não derruba o loop (apenas continua).
  - [ ] `timeoutMs = 1000` permite GC de sessões expiradas e timeouts a cada segundo.
  - [ ] Não chamar `read/write/recv/send` direto neste arquivo — apenas delega aos callbacks.

---

## E01-T05 — Implementar `EventLoop::reapClosed`

- **Owner:** M1
- **Tamanho:** S
- **Arquivos afetados:** `src/core/EventLoop.cpp`
- **Dependências:** E01-T04
- **Descrição:** percorrer `pollables_`, remover e deletar (`delete p;`) os que retornam `wantsClose() == true`. Deve ser chamado **fora** da iteração principal de `runOnce()` para evitar iterator invalidation.
- **Critérios de aceite:**
  - [ ] Iterator invalidation é evitado (uso de `erase()` retornando próximo iterador, ou cópia da lista).
  - [ ] Cada deleção gera log `INFO` com `fd` removido.
  - [ ] FDs são fechados automaticamente via destrutor (RAII) — sem `close()` manual.
  - [ ] Valgrind não reporta vazamento de `IPollable*` após `siege -c20 -t10s`.

---

## E01-T06 — Implementar `EventLoop::run`, `stop` e `isRunning`

- **Owner:** M1
- **Tamanho:** S
- **Arquivos afetados:** `src/core/EventLoop.cpp`
- **Dependências:** E01-T04
- **Descrição:** `run()` seta `running_ = true` e chama `runOnce(1000)` em loop até `running_` ser `false`. `stop()` seta `running_ = false`. `isRunning()` retorna `running_`.
- **Critérios de aceite:**
  - [ ] `run()` retorna apenas após `stop()` ser chamado.
  - [ ] `stop()` chamado de signal handler interrompe o loop após no máximo 1 segundo.
  - [ ] `isRunning()` é `true` durante `run()` e `false` após retorno.

---

## E01-T07 — Implementar `ListeningSocket` (construtor + `onReadable`)

- **Owner:** M1
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Server.cpp`, `include/core/Server.hpp`
- **Dependências:** E01-T01, E01-T02, E01-T03, E01-T04
- **Descrição:** construtor chama `socket_.bindAndListen(host, port)` e `setNonBlocking()`. `onReadable()` chama `accept()` em **loop** (até retornar -1) para drenar todas as conexões pendentes; para cada FD aceito, cria um `Client` e chama `loop_.add(client)`.
- **Critérios de aceite:**
  - [ ] `interest()` retorna sempre `POLLIN`.
  - [ ] `onReadable()` aceita múltiplas conexões na mesma iteração (loop até `EAGAIN`).
  - [ ] Cada `Client` criado é registrado no `EventLoop` via `loop_.add()`.
  - [ ] `wantsClose()` retorna sempre `false` (listening socket só fecha em `Server::stop`).
  - [ ] FD do cliente é colocado em modo não-bloqueante antes de criar o `Client`.

---

## E01-T08 — Implementar `Server::start` (agrupar configs + criar sockets)

- **Owner:** M1
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Server.cpp`
- **Dependências:** E01-T07, E02-T03 (precisa de `vector<ServerConfig>` válido)
- **Descrição:** agrupar `configs_` por par `(host, port)` único. Para cada par único, criar **um único** `ListeningSocket` passando todas as `ServerConfig` daquele endpoint (suporte a virtual hosting). Registrar cada `ListeningSocket` no `loop_` e chamar `loop_.run()`.
- **Critérios de aceite:**
  - [ ] Duas configs com `listen 8080` mas `server_name` diferentes compartilham um único listening socket.
  - [ ] Configs com portas diferentes geram múltiplos listening sockets.
  - [ ] Falha ao bindar uma porta gera erro claro e o servidor não inicia.
  - [ ] Logs `INFO` listam todas as portas/hosts em escuta no startup.

---

## E01-T09 — Implementar `Client::onReadable` + state machine

- **Owner:** M1
- **Tamanho:** L
- **Arquivos afetados:** `src/core/Client.cpp`, `include/core/Client.hpp`
- **Dependências:** E01-T04, E03-T01 (precisa de `RequestParser::feed`)
- **Descrição:** chamar `recv(fd, buf, sizeof(buf))`; se retorna 0 → `wantsClose_ = true`; se retorna `-1` com `EAGAIN` → aguarda próximo `POLLIN`; se retorna `>0` → `parser_.feed(buf, n, vhost.clientMaxBodySize)`. Tratar `FeedResult::COMPLETE` (transitar para `ROUTING` → `WRITING_RESPONSE`), `NEED_MORE` (continuar) e erros (`buildErrorResponse(parser_.errorStatus())`).
- **Critérios de aceite:**
  - [ ] Estados `READING_HEADERS → READING_BODY → ROUTING → WRITING_RESPONSE → DONE` transitam corretamente.
  - [ ] `interest()` retorna `POLLIN` em `READING_*`, `POLLOUT` em `WRITING_RESPONSE`, `0` em `DONE`.
  - [ ] `lastActivity_` é atualizado a cada `recv()` bem-sucedido.
  - [ ] Cliente que envia request inválido (linha malformada) recebe `400` e fecha após enviar.
  - [ ] Recv parcial (3 bytes de cada vez) funciona — request é montado ao longo de múltiplos `onReadable()`.

---

## E01-T10 — Implementar `Client::onWritable` (envio com offset + keep-alive)

- **Owner:** M1
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Client.cpp`
- **Dependências:** E01-T09, E04-T01 (precisa de `Response::toString`)
- **Descrição:** serializar `response_.toString()` em `outBuffer_` (uma vez); chamar `send(fd, outBuffer_.c_str() + outOffset_, remaining)`; incrementar `outOffset_`. Quando `outOffset_ >= outBuffer_.size()`: se `request_.keepAlive()` → `parser_.reset()`, voltar a `READING_HEADERS`; senão → `wantsClose_ = true`, `state_ = DONE`.
- **Critérios de aceite:**
  - [ ] Envio parcial (`send` retorna menos bytes que pedido) é tratado — próximo `onWritable` continua de onde parou.
  - [ ] Conexão keep-alive permite múltiplas requisições no mesmo socket.
  - [ ] `Connection: close` fecha a conexão após enviar resposta.
  - [ ] `EAGAIN` em `send` não derruba o cliente (apenas aguarda próximo `POLLOUT`).
  - [ ] HTTP/1.0 fecha por padrão; HTTP/1.1 mantém keep-alive por padrão.

---

## E01-T11 — Implementar `Client::matchVirtualHost`

- **Owner:** M1
- **Tamanho:** S
- **Arquivos afetados:** `src/core/Client.cpp`
- **Dependências:** E01-T09
- **Descrição:** ler `request_.header("Host")` e procurar em `vhosts_` o `ServerConfig` cujo `serverNames` contém esse valor. Se nenhum bater, retornar o primeiro vhost (comportamento padrão Nginx).
- **Critérios de aceite:**
  - [ ] `curl -H "Host: site-a.local"` é roteado para o vhost `site-a.local`.
  - [ ] `curl -H "Host: desconhecido"` cai no vhost default (primeiro declarado).
  - [ ] Comparação é case-insensitive.
  - [ ] Função retorna `const ServerConfig&` (sem cópia).

---

## E01-T12 — Tratamento de timeout de cliente

- **Owner:** M1
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Client.cpp`, `src/core/EventLoop.cpp`
- **Dependências:** E01-T09, E04-T02 (`makeError`)
- **Descrição:** o `EventLoop::runOnce()` percorre clientes a cada tick e marca `wantsClose_ = true` (após enviar 408) os que estão inativos há mais de N segundos (configurável via `Server`, default 60s).
- **Critérios de aceite:**
  - [ ] Cliente que abre conexão e não envia nada por 60s recebe `408 Request Timeout`.
  - [ ] Timeout não dispara em conexões com tráfego ativo.
  - [ ] Timeout reset a cada `recv()` ou `send()` bem-sucedido.

---

## E01-T13 — Sinal SIGINT/SIGTERM para shutdown gracioso

- **Owner:** M1
- **Tamanho:** S
- **Arquivos afetados:** `src/main.cpp`, `src/core/Server.cpp`
- **Dependências:** E01-T06
- **Descrição:** instalar handler para `SIGINT` e `SIGTERM` que apenas seta uma flag global (signal-safe). O handler não pode chamar funções não async-signal-safe. O `main()` checa a flag após `loop.run()` retornar.
- **Critérios de aceite:**
  - [ ] `Ctrl+C` no terminal encerra o servidor sem deixar FDs abertos (`valgrind --track-fds=yes` confirma).
  - [ ] `SIGTERM` (kill -15) também encerra graciosamente.
  - [ ] Conexões em andamento são finalizadas antes do shutdown.
  - [ ] Handler usa apenas variáveis `volatile sig_atomic_t`.

---

## Resumo de tarefas

| ID | Tarefa | Tamanho | Dependências |
|----|--------|---------|-------------|
| E01-T01 | Socket::bindAndListen | S | — |
| E01-T02 | Socket::setNonBlocking | S | T01 |
| E01-T03 | Socket::accept | S | T01 |
| E01-T04 | EventLoop::runOnce | L | T01, T02 |
| E01-T05 | EventLoop::reapClosed | S | T04 |
| E01-T06 | EventLoop::run/stop | S | T04 |
| E01-T07 | ListeningSocket | M | T01–T04 |
| E01-T08 | Server::start | M | T07, E02-T03 |
| E01-T09 | Client::onReadable + state machine | L | T04, E03-T01 |
| E01-T10 | Client::onWritable | M | T09, E04-T01 |
| E01-T11 | Client::matchVirtualHost | S | T09 |
| E01-T12 | Timeout de cliente | M | T09, E04-T02 |
| E01-T13 | SIGINT/SIGTERM handler | S | T06 |
