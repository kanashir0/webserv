# Overview — Webserv em 10 minutos

> Leia este documento primeiro. Ele resume tudo que você precisa para entender o projeto e começar a codar hoje. Os documentos longos ([README.md](../README.md), [tasks/PLANNING.md](../tasks/PLANNING.md)) viram **consulta**, não pré-requisito.

---

## 1. O que estamos construindo

Um servidor HTTP/1.1 (projeto **webserv** da 42) com três características inegociáveis:

1. **Single-thread** — um único processo, zero threads.
2. **Non-blocking** — nenhuma chamada de `read()`/`write()` pode travar o processo.
3. **Um único `poll()`** — todo o I/O passa por um ponto só.

Na prática: em vez de "uma thread por conexão", o servidor pergunta ao SO *"quais sockets têm dados prontos AGORA?"* (`poll()`) e só processa esses. É assim que Nginx funciona.

**Estado atual:** o esqueleto compila e roda, mas **quase tudo é stub**. Todo método vazio tem um comentário `// TODO Membro X` dizendo de quem é. `grep -rn "TODO" src/` mostra exatamente o que falta.

---

## 2. A arquitetura em uma imagem

```
                        main.cpp
                           │
          ConfigParser lê o .conf → vector<ServerConfig>
                           │
                        Server
              cria 1 ListeningSocket por porta
                           │
                    ╔══ EventLoop ══╗   ← ÚNICO lugar com poll()
                    ║  loop infinito ║
                    ╚═══════╦════════╝
            poll() diz "fd X está pronto", o loop despacha:
                            │
   ┌────────────────────────┼─────────────────────────┐
   ▼                        ▼                         ▼
ListeningSocket          Client                   CgiHandler
"conexão nova?"     "bytes chegaram/posso     "pipe do script
→ accept() cria      enviar?" → parseia,       python respondeu?"
  um Client          roteia, responde
```

**A ideia central (Reactor Pattern):** qualquer coisa que tenha um file descriptor implementa a interface `IPollable` ([include/core/IPollable.hpp](../include/core/IPollable.hpp)):

```cpp
int   fd()          // qual FD monitorar
short interest()    // quero ler (POLLIN) ou escrever (POLLOUT)?
void  onReadable()  // poll() disse: tem dados pra ler
void  onWritable()  // poll() disse: pode enviar
void  onHangup()    // o outro lado fechou
bool  wantsClose()  // "pode me deletar" (o loop deleta depois, nunca durante)
```

O `EventLoop` mantém uma lista de `IPollable*`, chama `poll()` e invoca os callbacks certos. **Ninguém mais no projeto chama `poll()`, `recv()` ou `send()` por conta própria** — isso é o que garante a nota no defense.

---

## 3. A vida de uma requisição (o fluxo que você precisa decorar)

```
1. Navegador conecta          → ListeningSocket::onReadable() → accept() → new Client
2. Bytes chegam               → Client::onReadable() → recv() → RequestParser::feed()
3. Parser completa            → Router::route(request, vhost)
4. Router acha a location     → GetHandler / PostHandler / DeleteHandler
5. Handler monta a resposta   → ResponseFactory → Response
6. Socket pronto p/ escrever  → Client::onWritable() → send() (parcial é normal!)
7. Resposta enviada           → wantsClose_ = true → EventLoop::reapClosed() deleta
```

O `Client` é uma **state machine**: `READING_HEADERS → READING_BODY → ROUTING → WRITING_RESPONSE → DONE`. O método `interest()` retorna `POLLIN` enquanto lê e `POLLOUT` enquanto escreve — é assim que o loop sabe o que monitorar.

O `RequestParser` também é uma state machine, porque o TCP fragmenta: `GET /index.html HTTP/1.1` pode chegar em 3 `recv()` separados. O parser acumula bytes e retorna `NEED_MORE` até ter uma requisição completa (`COMPLETE`) ou inválida (`BAD_REQUEST`, `BODY_TOO_LARGE`...).

---

## 4. Quem é dono do quê

| Membro | Branch | Módulos | Resumo da missão |
|--------|--------|---------|------------------|
| **M1** | `feat/core-network` | `src/core/`, `src/common/Socket.cpp`, `src/cgi/CgiHandler.cpp` | Fazer bytes entrarem e saírem: sockets, `EventLoop`, `Client`, fork/pipes do CGI |
| **M2** | `feat/parsers` | `src/config/`, `src/http/RequestParser.cpp`, `src/common/` | Transformar texto em estrutura: `.conf` → `ServerConfig`, bytes → `Request` |
| **M3** | `feat/http-logic` | `src/http/` (Router, handlers, Response, Factory), `src/session/`, `src/cgi/CgiEnv.cpp` | Decidir a resposta: roteamento, GET/POST/DELETE, páginas de erro, sessões |

**Os contratos entre vocês são os DTOs** — e eles já estão prontos nos headers:

- M2 produz um `Request` (imutável, só o parser escreve nele via `friend`).
- M3 consome o `Request` e produz um `Response`.
- M1 serializa o `Response` (`toString()`) e envia pelo socket.

Ou seja: **ninguém precisa esperar ninguém para começar**. Os `.hpp` em [include/](../include/) definem as assinaturas; cada um implementa o seu `.cpp` e testa com mocks.

---

## 5. Por onde cada um começa (primeira semana)

### M1 — comece por baixo, suba
1. `Socket::bindAndListen()` e `setNonBlocking()` — [src/common/Socket.cpp](../src/common/Socket.cpp) (socket/bind/listen/fcntl, ~50 linhas).
2. `EventLoop::runOnce()` — [src/core/EventLoop.cpp](../src/core/EventLoop.cpp): montar o `pollfd[]`, chamar `poll()`, despachar callbacks. **É o coração do projeto.**
3. `ListeningSocket::onReadable()` e `Client::onReadable()/onWritable()` — [src/core/](../src/core/).
4. *Enquanto o parser do M2 não existe:* responda qualquer requisição com uma string HTTP fixa (`"HTTP/1.1 200 OK\r\n..."`) só para ver `curl` funcionar ponta-a-ponta.

### M2 — dois parsers, teste com strings
1. `ConfigParser` — [src/config/ConfigParser.cpp](../src/config/ConfigParser.cpp): tokenizar e preencher `ServerConfig`/`LocationConfig`. Teste com `parseString("server { listen 8080; }")` direto, sem arquivo.
2. `RequestParser::feed()` — [src/http/RequestParser.cpp](../src/http/RequestParser.cpp): request line → headers → body. **Teste alimentando byte a byte** — se funcionar fragmentado, funciona sempre.
3. Deixe `chunked` por último; `Content-Length` primeiro.

### M3 — de dentro pra fora
1. `Response::toString()` e `ResponseFactory::makeError()` — sem isso ninguém responde nada, nem erro.
2. `ServerConfig::findLocation()` — longest-prefix match (`/upload/img` casa com `/upload`, não com `/`).
3. `Router::route()` — checa location → método permitido (405) → redirect → despacha pro handler.
4. `GetHandler` (arquivo estático + index + autoindex) → depois `DeleteHandler` (fácil) → depois `PostHandler` (upload).
5. Teste os handlers construindo um `Request` fake — não precisa do servidor rodando.

### Regra de ouro
> Bloqueado há mais de 1 dia esperando outro membro? **Mocka a dependência e segue.** Os headers são o contrato; a integração real vem depois.

**CGI fica para a semana 3** e é feito em dupla (M1 faz fork/pipes, M3 faz as variáveis de ambiente). Não comecem antes.

---

## 6. As 5 regras que reprovam se violadas

| Regra | Como o esqueleto já protege você |
|-------|----------------------------------|
| `poll()` em um único lugar | Só existe em `EventLoop::runOnce()`. Não chame em outro lugar. |
| Nunca `recv()`/`send()` sem o poll autorizar | Só faça I/O dentro de `onReadable()`/`onWritable()`. |
| `fork()` só para CGI | Só em `CgiHandler::start()`. |
| Zero threads | Não use pthread. Ponto. |
| C++98 estrito | Sem `nullptr`, `auto`, `std::to_string`, smart pointers, range-for. |

E três convenções internas que evitam bugs clássicos:

- **FDs sempre via `FileDescriptor` (RAII)** — nunca chame `close()` na mão; o destrutor fecha. Transferência de posse via `release()`.
- **Nunca delete um `IPollable` dentro de um callback** — sete `wantsClose_ = true` e deixe `reapClosed()` deletar após a iteração (deletar durante a iteração = crash).
- **`Response::setBody()` já atualiza `Content-Length`** — não sete esse header manualmente.

---

## 7. Comandos do dia a dia

```bash
make                              # compila (c++98, -Wall -Wextra -Werror -pedantic)
./webserv conf/default.conf       # roda na porta 8080
curl http://localhost:8080/       # smoke test

make test                         # bateria de curl (tests/scripts/curl-suite.sh)
tests/scripts/run-valgrind.sh conf/default.conf   # leaks de memória E de FD
tests/scripts/run-siege.sh        # stress test
```

Antes de todo PR: `make re` sem nenhum warning + valgrind limpo (0 bytes lost, 0 FDs abertos). Fluxo git: branch da feature → PR pequeno (<500 linhas) → 1 review → squash merge na `main`.

---

## 8. Mapa dos documentos (quando aprofundar)

| Documento | Quando ler |
|-----------|-----------|
| **Este arquivo** | Antes de tudo |
| [tasks/epic-0X-*.md](../tasks/) | Ao pegar uma tarefa — cada épico tem tarefas com critérios de aceite |
| [README.md](../README.md) | Como **referência por classe**: antes de implementar um método, leia a seção dele (Ctrl+F pelo nome). Não leia de ponta a ponta. |
| [tasks/PLANNING.md](../tasks/PLANNING.md) | Roadmap de sprints, bloqueios cruzados, definition of done |
| [docs/git-workflow.md](git-workflow.md) / [docs/testing.md](testing.md) | Convenções de PR e roteiro de testes |

Há também skills do Claude Code por área (`.claude/skills/`): `webserv-core` (M1), `webserv-parsers` (M2), `webserv-http` (M3), e `webserv-rules` para auditar as restrições antes de cada PR.
