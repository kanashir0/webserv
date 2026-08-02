# Épico 01 — Motor de Rede e Reactor Pattern

> **Dono primário:** Membro 1 (M1)
> **Branch:** `feat/core-network`
> **Valor entregue:** servidor capaz de aceitar conexões TCP, gerenciar múltiplos clientes simultâneos via `poll()` único e mover dados entre socket ↔ parser ↔ resposta sem nunca bloquear o processo.
> **Critério de "épico pronto":** `./webserv conf/default.conf` aceita conexões, mantém múltiplos clientes em paralelo e responde com mocks/handlers sem travar (`siege -c10 -t10s` sem queda).

> **Status do épico (auditoria de 02/08/2026):** 🟡 **5 ✅ / 8 ⚠️ / 0 ❌** — todas as tarefas
> foram implementadas e o servidor sobe e responde, mas 8 delas têm defeitos que não cumprem
> os próprios critérios de aceite. Ver [Bugs e ajustes abertos](#bugs-e-ajustes-abertos).
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

---

## ✅ E01-T01 — Implementar `Socket::bindAndListen`

- **Owner:** M1
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/common/Socket.cpp`, `include/common/Socket.hpp`
- **Dependências:** nenhuma
- **Descrição:** implementar a sequência `socket() → setsockopt(SO_REUSEADDR) → bind() → listen(backlog=128)` na função `Socket::bindAndListen(host, port, backlog)`. Em erro de qualquer syscall, lançar `std::runtime_error` com mensagem de `errno` (via `strerror`).
- **Critérios de aceite:**
  - [x] FD do socket é armazenado em `FileDescriptor` interno (RAII).
  - [x] `SO_REUSEADDR` é aplicado antes do `bind()` para permitir reinicialização rápida do servidor.
  - [x] Erros de `bind` em portas privilegiadas (<1024) e portas em uso geram exceção. ← mensagem é `"BIND FALHOU"`, sem `strerror(errno)`; melhorar junto de BUG-01-10
  - [x] `host == "0.0.0.0"` resulta em escuta em todas as interfaces; `host == "127.0.0.1"` apenas localhost.
  - [x] Após `bindAndListen`, `socket_.fd() >= 0` e o servidor consegue iniciar.

---

## ⚠️ E01-T02 — Implementar `Socket::setNonBlocking`

- **Owner:** M1
- **Status:** ⚠️ REABRIR — ver [BUG-01-01](#bug-01-01--setnonblocking-descarta-as-flags-existentes-do-fd)
- **Tamanho:** S
- **Arquivos afetados:** `src/common/Socket.cpp`
- **Dependências:** E01-T01
- **Descrição:** aplicar `fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)` para que `recv/send/accept` retornem imediatamente com `EAGAIN/EWOULDBLOCK` em vez de bloquear.
- **Critérios de aceite:**
  - [ ] Recebe `int fd` e aplica `O_NONBLOCK` **preservando outras flags**. ← hoje faz `fcntl(fd, F_SETFL, O_NONBLOCK)` sem ler `F_GETFL`, apagando as demais
  - [x] Falha em `fcntl` lança `std::runtime_error`. ← sem `errno` na mensagem
  - [x] Após chamada, `recv` em socket vazio retorna -1 com `errno == EAGAIN`.

---

## ⚠️ E01-T03 — Implementar `Socket::accept`

- **Owner:** M1
- **Status:** ⚠️ REABRIR — ver [BUG-01-02](#bug-01-02--socketacceptconnection-lança-exceção-em-erro-não-eagain)
- **Tamanho:** S
- **Arquivos afetados:** `src/common/Socket.cpp`
- **Dependências:** E01-T01
- **Descrição:** chamar `::accept(fd, &addr, &len)` e retornar o FD do cliente. Em `EAGAIN/EWOULDBLOCK`, retornar `-1` sem lançar exceção (é fluxo normal quando todas as conexões já foram drenadas).
- **Critérios de aceite:**
  - [ ] Preenche `outAddr` com IP/porta do cliente. ← `sockaddr_in` é local e descartado; o IP do cliente não chega a lugar nenhum (necessário para `REMOTE_ADDR` em E06-T01)
  - [x] Retorna `-1` em `EAGAIN/EWOULDBLOCK` sem logar erro.
  - [ ] Retorna `-1` e loga `WARN` em outros erros. ← hoje lança `std::runtime_error`, que derruba o servidor
  - [x] FD retornado **não** é gerenciado pelo `Socket` (ownership transferido ao chamador via `release()`).

---

## ✅ E01-T04 — Implementar `EventLoop::runOnce` (poll + dispatch)

- **Owner:** M1
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** L
- **Arquivos afetados:** `src/core/EventLoop.cpp`, `include/core/EventLoop.hpp`
- **Dependências:** E01-T01, E01-T02
- **Descrição:** este é o **único** ponto de `poll()` em todo o projeto. Construir array `pollfd[]` a partir de `pollables_` (preenchendo `.fd = p->fd()` e `.events = p->interest()`), chamar `poll(fds, n, timeoutMs)`, e despachar eventos: `POLLIN → onReadable()`, `POLLOUT → onWritable()`, `POLLHUP|POLLERR → onHangup()`. Após o dispatch, chamar `reapClosed()`.
- **Critérios de aceite:**
  - [x] Apenas 1 ocorrência de `poll(` em todo o código-fonte — confirmado: `src/core/EventLoop.cpp:37`.
  - [x] Despacho funciona para `ListeningSocket`, `Client` e `CgiHandler` sem código específico de tipo.
  - [x] `EINTR` em `poll()` não derruba o loop (apenas continua).
  - [x] `timeoutMs = 1000` permite GC de sessões expiradas e timeouts a cada segundo.
  - [x] Não chamar `read/write/recv/send` direto neste arquivo — apenas delega aos callbacks.

> **Nota:** a chamada `p->checkTimeout(now, timeoutMs)` neste arquivo passa o timeout do
> `poll()` em **milissegundos** para um parâmetro consumido em **segundos**. O defeito
> pertence a E01-T12 ([BUG-01-06](#bug-01-06--timeout-de-cliente-usa-milissegundos-como-segundos)),
> mas a correção é aqui.

---

## ✅ E01-T05 — Implementar `EventLoop::reapClosed`

- **Owner:** M1
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/core/EventLoop.cpp`
- **Dependências:** E01-T04
- **Descrição:** percorrer `pollables_`, remover e deletar (`delete p;`) os que retornam `wantsClose() == true`. Deve ser chamado **fora** da iteração principal de `runOnce()` para evitar iterator invalidation.
- **Critérios de aceite:**
  - [x] Iterator invalidation é evitado (`it = pollables_.erase(it)`).
  - [x] Cada deleção gera log `INFO` com `fd` removido.
  - [x] FDs são fechados automaticamente via destrutor (RAII) — sem `close()` manual.
  - [ ] Valgrind não reporta vazamento de `IPollable*` após `siege -c20 -t10s`. ← não executado ainda (E08-T03). O reap em regime funciona; o vazamento conhecido é no **shutdown**, ver [BUG-01-07](#bug-01-07--eventloop-não-libera-os-ipollable-restantes-no-shutdown)

---

## ✅ E01-T06 — Implementar `EventLoop::run`, `stop` e `isRunning`

- **Owner:** M1
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/core/EventLoop.cpp`
- **Dependências:** E01-T04
- **Descrição:** `run()` seta `running_ = true` e chama `runOnce(1000)` em loop até `running_` ser `false`. `stop()` seta `running_ = false`. `isRunning()` retorna `running_`.
- **Critérios de aceite:**
  - [x] `run()` retorna apenas após `stop()` ser chamado.
  - [x] `stop()` chamado de signal handler interrompe o loop após no máximo 1 segundo (via flag `g_shutdown` checada a cada iteração).
  - [x] `isRunning()` é `true` durante `run()` e `false` após retorno.

---

## ⚠️ E01-T07 — Implementar `ListeningSocket` (construtor + `onReadable`)

- **Owner:** M1
- **Status:** ⚠️ REABRIR — ver [BUG-01-03](#bug-01-03--listeningsocketonreadable-cria-client-com-fd--1)
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Server.cpp`, `include/core/Server.hpp`
- **Dependências:** E01-T01, E01-T02, E01-T03, E01-T04
- **Descrição:** construtor chama `socket_.bindAndListen(host, port)` e `setNonBlocking()`. `onReadable()` chama `accept()` em **loop** (até retornar -1) para drenar todas as conexões pendentes; para cada FD aceito, cria um `Client` e chama `loop_.add(client)`.
- **Critérios de aceite:**
  - [x] `interest()` retorna sempre `POLLIN`.
  - [x] `onReadable()` aceita múltiplas conexões na mesma iteração (loop até `EAGAIN`).
  - [x] Cada `Client` criado é registrado no `EventLoop` via `loop_.add()`.
  - [x] `wantsClose()` retorna sempre `false`.
  - [x] FD do cliente é colocado em modo não-bloqueante antes de criar o `Client` (dentro de `Socket::acceptConnection`).
  - [ ] Nenhum caminho cria `Client` com FD inválido. ← o `if (client_fd < 0)` só faz `break` no EAGAIN; qualquer outro retorno negativo cai em `new Client(-1, ...)`

---

## ⚠️ E01-T08 — Implementar `Server::start` (agrupar configs + criar sockets)

- **Owner:** M1
- **Status:** ⚠️ REABRIR — ver [BUG-01-04](#bug-01-04--serverstart-agrupa-vhosts-errado-e-sempre-tenta-um-bind-redundante)
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Server.cpp`
- **Dependências:** E01-T07, E02-T03
- **Descrição:** agrupar `configs_` por par `(host, port)` único. Para cada par único, criar **um único** `ListeningSocket` passando todas as `ServerConfig` daquele endpoint (suporte a virtual hosting). Registrar cada `ListeningSocket` no `loop_` e chamar `loop_.run()`.
- **Critérios de aceite:**
  - [x] Duas configs com `listen 8080` mas `server_name` diferentes compartilham um único listening socket. ← funciona **por acidente**: o `addServer()` acerta, mas o código segue e tenta criar um segundo listener que falha no `bind`
  - [ ] Configs com portas diferentes geram múltiplos listening sockets. ← o `else break` sai do loop no primeiro listener que não bate; a lógica quebra com 3+ servers em ordem desfavorável
  - [ ] Falha ao bindar uma porta gera erro claro e o servidor **não inicia**. ← hoje o erro é engolido com `std::cout` e o servidor sobe mesmo assim
  - [x] Logs listam as portas/hosts em escuta no startup. ← usa `LOG_WARN` para mensagem informativa (BUG-01-10)

---

## ⚠️ E01-T09 — Implementar `Client::onReadable` + state machine

- **Owner:** M1
- **Status:** ⚠️ REABRIR — ver [BUG-01-08](#bug-01-08--client-nunca-usa-o-estado-reading_body-e-ignora-errno-no-recv)
- **Tamanho:** L
- **Arquivos afetados:** `src/core/Client.cpp`, `include/core/Client.hpp`
- **Dependências:** E01-T04, E03-T01
- **Descrição:** chamar `recv(fd, buf, sizeof(buf))`; se retorna 0 → `wantsClose_ = true`; se retorna `-1` com `EAGAIN` → aguarda próximo `POLLIN`; se retorna `>0` → `parser_.feed(buf, n, vhost.clientMaxBodySize)`. Tratar `FeedResult::COMPLETE` (transitar para `ROUTING` → `WRITING_RESPONSE`), `NEED_MORE` (continuar) e erros (`buildErrorResponse(parser_.errorStatus())`).
- **Critérios de aceite:**
  - [ ] Estados `READING_HEADERS → READING_BODY → ROUTING → WRITING_RESPONSE → DONE` transitam corretamente. ← `READING_BODY` e `ROUTING` **nunca são atribuídos**; o `Client` salta de `READING_HEADERS` para `WRITING_RESPONSE`
  - [x] `interest()` retorna `POLLIN` em `READING_*`, `POLLOUT` em `WRITING_RESPONSE`, `0` em `DONE`.
  - [x] `lastActivity_` é atualizado a cada `recv()` bem-sucedido.
  - [x] Cliente que envia request inválido recebe `400` e fecha após enviar. ← verificado (`GET / HTTP/1.1` sem `Host` → 400)
  - [x] Recv parcial funciona — request é montado ao longo de múltiplos `onReadable()`.
  - [ ] `recv() < 0` distingue `EAGAIN` de erro real. ← hoje faz `return` cego sem olhar `errno`; erro real mantém a conexão presa até o timeout

> **Decisão pendente:** ou se implementa a transição real para `READING_BODY`/`ROUTING`, ou
> se removem os dois estados do enum — quem tem a máquina de estados de verdade é o
> `RequestParser`. A segunda opção é a mais barata e é a recomendada.

---

## ✅ E01-T10 — Implementar `Client::onWritable` (envio com offset + keep-alive)

- **Owner:** M1
- **Status:** ✅ CONCLUÍDA — mas o keep-alive descarta pipelining, ver [BUG-01-09](#bug-01-09--keep-alive-descarta-requests-em-pipelining)
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Client.cpp`
- **Dependências:** E01-T09, E04-T01
- **Descrição:** serializar `response_.toString()` em `outBuffer_` (uma vez); chamar `send(fd, outBuffer_.c_str() + outOffset_, remaining)`; incrementar `outOffset_`. Quando `outOffset_ >= outBuffer_.size()`: se `request_.keepAlive()` → `parser_.reset()`, voltar a `READING_HEADERS`; senão → `wantsClose_ = true`, `state_ = DONE`.
- **Critérios de aceite:**
  - [x] Envio parcial é tratado — próximo `onWritable` continua de onde parou.
  - [x] Conexão keep-alive permite múltiplas requisições no mesmo socket. ← verificado com 2 requests na mesma conexão
  - [x] `Connection: close` fecha a conexão após enviar resposta.
  - [x] `EAGAIN` em `send` não derruba o cliente.
  - [x] HTTP/1.0 fecha por padrão; HTTP/1.1 mantém keep-alive por padrão.

---

## ⚠️ E01-T11 — Implementar `Client::matchVirtualHost`

- **Owner:** M1
- **Status:** ⚠️ REABRIR — ver [BUG-01-05](#bug-01-05--matchvirtualhost-não-remove-a-porta-do-header-host)
- **Tamanho:** S
- **Arquivos afetados:** `src/core/Client.cpp`
- **Dependências:** E01-T09
- **Descrição:** ler `request_.header("Host")` e procurar em `vhosts_` o `ServerConfig` cujo `serverNames` contém esse valor. Se nenhum bater, retornar o primeiro vhost (comportamento padrão Nginx).
- **Critérios de aceite:**
  - [ ] `curl -H "Host: site-a.local"` é roteado para o vhost `site-a.local`. ← só funciona quando o header vem **sem porta**; browsers e o curl padrão mandam `Host: site-a.local:8080` e caem no default
  - [x] `curl -H "Host: desconhecido"` cai no vhost default (primeiro declarado).
  - [ ] Comparação é case-insensitive. ← usa `==` de `std::string`
  - [x] Função retorna `const ServerConfig&` (sem cópia).

---

## ⚠️ E01-T12 — Tratamento de timeout de cliente

- **Owner:** M1
- **Status:** ⚠️ REABRIR — implementada, mas inoperante na prática. Ver [BUG-01-06](#bug-01-06--timeout-de-cliente-usa-milissegundos-como-segundos)
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Client.cpp`, `src/core/EventLoop.cpp`
- **Dependências:** E01-T09, E04-T02
- **Descrição:** o `EventLoop::runOnce()` percorre clientes a cada tick e marca `wantsClose_ = true` (após enviar 408) os que estão inativos há mais de N segundos (configurável via `Server`, default 60s).
- **Critérios de aceite:**
  - [ ] Cliente que abre conexão e não envia nada por 60s recebe `408 Request Timeout`. ← o valor passado é `timeoutMs` (1000), interpretado como segundos → ~16 minutos
  - [x] Timeout não dispara em conexões com tráfego ativo.
  - [x] Timeout reset a cada `recv()` ou `send()` bem-sucedido.
  - [ ] O valor de timeout é configurável (default 60s). ← hoje é o timeout do `poll()`, não um parâmetro próprio

---

## ⚠️ E01-T13 — Sinal SIGINT/SIGTERM para shutdown gracioso

- **Owner:** M1
- **Status:** ⚠️ REABRIR — ver [BUG-01-07](#bug-01-07--eventloop-não-libera-os-ipollable-restantes-no-shutdown)
- **Tamanho:** S
- **Arquivos afetados:** `src/main.cpp`, `src/core/Server.cpp`
- **Dependências:** E01-T06
- **Descrição:** instalar handler para `SIGINT` e `SIGTERM` que apenas seta uma flag global (signal-safe). O handler não pode chamar funções não async-signal-safe. O `main()` checa a flag após `loop.run()` retornar.
- **Critérios de aceite:**
  - [ ] `Ctrl+C` encerra o servidor sem deixar FDs abertos. ← `~EventLoop` não deleta os `IPollable*` restantes; cada `Client` vivo vaza memória **e** FD
  - [x] `SIGTERM` (kill -15) também encerra graciosamente.
  - [ ] Conexões em andamento são finalizadas antes do shutdown. ← o loop simplesmente para; respostas em voo são perdidas
  - [x] Handler usa apenas `volatile sig_atomic_t`.

---

## Resumo de tarefas

| ID | Tarefa | Status | Tamanho | Dependências |
|----|--------|--------|---------|-------------|
| E01-T01 | Socket::bindAndListen | ✅ | S | — |
| E01-T02 | Socket::setNonBlocking | ⚠️ BUG-01-01 | S | T01 |
| E01-T03 | Socket::accept | ⚠️ BUG-01-02 | S | T01 |
| E01-T04 | EventLoop::runOnce | ✅ | L | T01, T02 |
| E01-T05 | EventLoop::reapClosed | ✅ | S | T04 |
| E01-T06 | EventLoop::run/stop | ✅ | S | T04 |
| E01-T07 | ListeningSocket | ⚠️ BUG-01-03 | M | T01–T04 |
| E01-T08 | Server::start | ⚠️ BUG-01-04 | M | T07, E02-T03 |
| E01-T09 | Client::onReadable + state machine | ⚠️ BUG-01-08 | L | T04, E03-T01 |
| E01-T10 | Client::onWritable | ✅ (BUG-01-09) | M | T09, E04-T01 |
| E01-T11 | Client::matchVirtualHost | ⚠️ BUG-01-05 | S | T09 |
| E01-T12 | Timeout de cliente | ⚠️ BUG-01-06 | M | T09, E04-T02 |
| E01-T13 | SIGINT/SIGTERM handler | ⚠️ BUG-01-07 | S | T06 |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`, com o
> binário em execução. Este épico concentra a maior parte da dívida do projeto — a
> **Fase A** da ordem de ataque em [`PLANNING.md`](PLANNING.md) é fechar estes bugs antes de
> começar o CGI.

### BUG-01-01 — `setNonBlocking` descarta as flags existentes do FD

- **Origem:** E01-T02
- **Onde:** `src/common/Socket.cpp:54`
- **Sintoma:** `fcntl(server_fd, F_SETFL, O_NONBLOCK)` sobrescreve o conjunto inteiro de
  flags do descritor em vez de acrescentar `O_NONBLOCK` ao que já existia.
- **Esperado:** `fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)`.
- **Severidade:** Baixa — inofensivo para sockets recém-criados, que não têm outras flags.
  Vira problema real assim que os pipes do CGI (E06-T03) passarem por aqui.

### BUG-01-02 — `Socket::acceptConnection` lança exceção em erro não-EAGAIN

- **Origem:** E01-T03
- **Onde:** `src/common/Socket.cpp:45`
- **Sintoma:** qualquer erro de `accept()` diferente de `EAGAIN/EWOULDBLOCK` lança
  `std::runtime_error`, que propaga por `onReadable` → `runOnce` → `run` → `main` e derruba
  o processo. `ECONNABORTED` (cliente desiste entre o SYN e o accept) é rotina em rede real.
- **Esperado:** retornar `-1` e logar `WARN`, conforme o critério de aceite da própria
  tarefa.
- **Severidade:** **Alta** — queda esperada sob `siege -c50` (E08-T02).

### BUG-01-03 — `ListeningSocket::onReadable` cria `Client` com FD -1

- **Origem:** E01-T07
- **Onde:** `src/core/Server.cpp:35-38`
- **Sintoma:**

  ```cpp
  if (client_fd < 0) {
      if (errno == EAGAIN)
          break;
  }
  // ← sem else: segue para new Client(client_fd, ...) com client_fd == -1
  ```

  Qualquer retorno negativo que não seja `EAGAIN` cai no `new Client(-1, ...)` e o `while
  (true)` nunca termina.
- **Esperado:** `break` (ou `continue`) em **todo** retorno negativo; só criar `Client` com
  FD válido.
- **Severidade:** **Alta** — hoje está mascarado por BUG-01-02 (o accept lança antes de
  retornar -1). Corrigir BUG-01-02 sem corrigir este transforma uma queda em um loop
  infinito, que é pior.

### BUG-01-04 — `Server::start` agrupa vhosts errado e sempre tenta um bind redundante

- **Origem:** E01-T08
- **Onde:** `src/core/Server.cpp:73-99`
- **Sintoma:** três defeitos no mesmo bloco:
  1. O `else { break; }` dentro do loop de listeners aborta a busca no **primeiro** listener
     que não casa, em vez de continuar procurando. Com 3+ servers em ordem desfavorável, o
     agrupamento por `(host, port)` falha.
  2. Depois do `addServer()`, o código segue e cria um `ListeningSocket` novo de qualquer
     jeito → `bind()` falha com `EADDRINUSE`, e o log mostra
     `FALHA EM BINDAR LISTENEN (SERVIDOR)BIND FALHOU` a cada server extra na mesma porta.
     Verificado com dois `server{}` em `listen 8080`.
  3. A exceção é engolida com `std::cout` (debug em produção) e o servidor sobe mesmo com
     um endpoint que não subiu.
- **Esperado:** agrupar por `(host, port)` num `std::map` antes de criar qualquer socket;
  criar exatamente um `ListeningSocket` por chave; falha de `bind` aborta o startup com
  mensagem clara via `Logger`.
- **Severidade:** **Alta** — o virtual hosting só funciona por acidente e o log de startup
  mente sobre o estado do servidor.

### BUG-01-05 — `matchVirtualHost` não remove a porta do header `Host`

- **Origem:** E01-T11
- **Onde:** `src/core/Client.cpp:130-141`
- **Sintoma:** a comparação é `host == serverName` com o valor bruto do header. Clientes
  reais mandam `Host: localhost:8080`, que nunca casa com `server_name localhost` → **todo
  request cai no primeiro vhost declarado**. Com um único `server{}` o bug é invisível, o
  que o torna traiçoeiro. Também é case-sensitive, contra a RFC 7230 §5.4.
- **Esperado:** cortar tudo a partir do `:` antes de comparar e usar
  `StringUtils::iequals` (já existe em `src/common/StringUtils.cpp`).
- **Severidade:** **Alta** — o virtual hosting é item de defense (E08-T06).

### BUG-01-06 — Timeout de cliente usa milissegundos como segundos

- **Origem:** E01-T12
- **Onde:** `src/core/EventLoop.cpp:47` (chamada) e `src/core/Client.cpp:147` (consumo)
- **Sintoma:** `runOnce(int timeoutMs)` repassa `timeoutMs` (1000) para
  `checkTimeout(now, timeout)`, que compara com `now - lastActivity_` em **segundos**. O
  timeout efetivo é 1000 s ≈ 16 min, não os 60 s especificados. Verificado: conexão ociosa
  não recebeu 408.
- **Esperado:** timeout de cliente como parâmetro próprio do `Server`/`Client` (default
  60 s), independente do timeout do `poll()`.
- **Severidade:** **Alta** — E08-T05 exige que uma conexão aberta e abandonada não segure
  recursos; hoje segura por 16 minutos.

### BUG-01-07 — `~EventLoop` não libera os `IPollable*` restantes no shutdown

- **Origem:** E01-T13 (critério de aceite), E01-T05
- **Onde:** `src/core/EventLoop.cpp:4`
- **Sintoma:** o destrutor é vazio. Ao sair de `run()` por `SIGINT`, todo `Client` ainda
  vivo em `pollables_` vaza memória e FD. Os `ListeningSocket` são salvos porque `~Server`
  os deleta separadamente — mas isso significa que `pollables_` guarda ponteiros com dois
  regimes de ownership diferentes, o que precisa ficar explícito antes de o CGI entrar
  (ver E06-T07).
- **Esperado:** `~EventLoop` deleta o que sobrou, **ou** o ownership passa a ser inteiramente
  do `EventLoop` e `~Server` para de deletar os listeners. Escolher um dos dois e
  documentar.
- **Severidade:** Média — bloqueia o critério "0 FDs vazados" de E08-T03.

### BUG-01-08 — `Client` nunca usa o estado `READING_BODY` e ignora `errno` no `recv`

- **Origem:** E01-T09
- **Onde:** `src/core/Client.cpp:50-78`
- **Sintoma:** dois defeitos:
  1. O enum `Client::State` declara `READING_BODY` e `ROUTING`, mas nenhum dos dois é
     atribuído em lugar nenhum — o `Client` vai direto de `READING_HEADERS` para
     `WRITING_RESPONSE`. Estado morto confunde quem lê e quebra o critério de aceite.
  2. `if (ret < 0) return;` não distingue `EAGAIN` de erro real; um `ECONNRESET` deixa a
     conexão presa até o timeout (que, por BUG-01-06, é de 16 min).
- **Esperado:** remover os estados mortos do enum (recomendado — o `RequestParser` já tem a
  máquina de estados real) ou implementar as transições; e fechar a conexão quando `errno`
  não for `EAGAIN/EWOULDBLOCK`.
- **Severidade:** Média.

### BUG-01-09 — Keep-alive descarta requests em pipelining

- **Origem:** E01-T10 (anula o critério de aceite de E03-T08)
- **Onde:** `src/core/Client.cpp:113`
- **Sintoma:** `RequestParser::take()` foi escrito com o cuidado de **preservar** os bytes
  residuais do buffer (a próxima request em pipelining). Mas o `Client`, ao reciclar a
  conexão em keep-alive, chama `parser_.reset()`, cujo primeiro efeito é `buf_.clear()`. Se
  o cliente mandar duas requests no mesmo pacote, a segunda é silenciosamente descartada.
- **Esperado:** não chamar `reset()` no caminho de keep-alive; após `take()`, chamar
  `feed(NULL, 0, maxBody)` para processar o que já está no buffer.
- **Severidade:** Média — pipelining é raro em clientes reais, mas o `RequestParser` já
  pagou o custo de suportá-lo e o `Client` joga fora.

### BUG-01-10 — Resíduos de debug e mensagens de erro sem `errno`

- **Origem:** E01-T01, E01-T08, E01-T13
- **Onde:** `src/main.cpp:30` (`"webserv starting (skeleton, no real I/O yet)"` —
  desatualizado), `src/main.cpp:31-33` (código comentado), `src/core/Server.cpp:90`
  (`LOG_WARN` para mensagem informativa), `src/core/Server.cpp:97` (`std::cout` de debug),
  `src/common/Socket.cpp` (mensagens `"BIND FALHOU"` / `"LISTEN FALHOU"` /
  `"FCNTL FALHOU"` sem `strerror(errno)`).
- **Esperado:** limpar tudo; mensagens de exceção incluem `strerror(errno)`, conforme
  E01-T01.
- **Severidade:** Baixa — mas E08-T08 audita explicitamente "sem `printf`/`std::cout` de
  debug, sem código comentado".
