# Membro 1 — Motor de rede (`src/core/`, `Socket`, execução do CGI)

> Você é o dono da parte que a régua mais ataca: o `poll()`, o I/O e o `fork()`.
> Visão geral em [README.md](README.md).

**Arquivos:** [EventLoop](../../src/core/EventLoop.cpp) · [Server/ListeningSocket](../../src/core/Server.cpp) ·
[Client](../../src/core/Client.cpp) · [Socket](../../src/common/Socket.cpp) ·
[FileDescriptor](../../src/common/FileDescriptor.cpp) · [CgiHandler](../../src/cgi/CgiHandler.cpp)

---

## 1. `EventLoop` — o coração

[src/core/EventLoop.cpp:17](../../src/core/EventLoop.cpp#L17)

```cpp
void EventLoop::runOnce(int timeoutMs, int timeoutSec) {
    std::vector<pollfd> fds;
    for (cada IPollable* p : pollables_) {          // 1. monta o array
        fd.fd = p->fd(); fd.events = p->interest(); fd.revents = 0;
    }
    int n = poll(&fds[0], fds.size(), timeoutMs);   // 2. ÚNICO poll() do projeto
    if (n < 0) { if (errno == EINTR) return; throw ...; }

    for (cada p : pollables_) p->checkTimeout(now, timeoutSec);  // 3. timeouts

    for (i = 0; i < fds.size(); i++) {              // 4. despacha só o que ficou pronto
        if (revents & POLLIN)               p->onReadable();
        if (revents & POLLOUT)              p->onWritable();
        if (revents & (POLLHUP | POLLERR))  p->onHangup();
    }
}
```

E o laço principal ([:63](../../src/core/EventLoop.cpp#L63)):

```cpp
while (running_) {
    runOnce(1000, 60);
    reapClosed();                 // deleta os que pediram para fechar
    if (tickHandler_) tickHandler_->onTick();   // GC de sessões (bônus)
    if (g_shutdown) stop();       // SIGINT/SIGTERM: saída limpa
}
```

**Pontos para defender:**

- **Um `poll()` só.** `grep -rn "poll(" src/` mostra uma única chamada (as demais
  linhas são comentários). Ninguém mais chama `poll`, `select` ou `epoll`.
- **Leitura e escrita na mesma chamada.** `events` vem de `interest()`, que varia
  por objeto e por estado: no mesmo array pode haver um listener com `POLLIN`, um
  cliente lendo com `POLLIN` e outro escrevendo com `POLLOUT`.
- **Por que `errno` aqui é legal.** A proibição da régua vale para
  `read/recv/write/send`. Depois do `poll()`, `EINTR` significa "um sinal
  interrompeu", não erro — o `SIGINT` do Ctrl-C cai exatamente aqui.
- **Timeout de 1000 ms** garante que `checkTimeout()` e o GC de sessões rodem
  mesmo sem nenhum evento de rede.
- **Índice `i` de `fds` casa com `pollables_[i]`** porque nada é removido durante o
  despacho: quem quer morrer seta `wantsClose_` e só é deletado em `reapClosed()`,
  depois do laço ([:80](../../src/core/EventLoop.cpp#L80)). Callbacks podem
  *adicionar* (o `CgiHandler`), e isso é seguro: o novo entra no fim e só é
  observado na próxima iteração.
- **Dono da memória.** O destrutor deleta todos os `IPollable*`
  ([:4](../../src/core/EventLoop.cpp#L4)) — é por isso que o valgrind fecha em zero.

---

## 2. `Socket` e `ListeningSocket` — como as conexões entram

[src/common/Socket.cpp:7](../../src/common/Socket.cpp#L7)

```
socket(AF_INET, SOCK_STREAM, 0)
  → setsockopt(SO_REUSEADDR)      // reusar porta em TIME_WAIT ao reiniciar
  → fcntl(fd, F_SETFL, O_NONBLOCK)
  → bind()  → listen(backlog)
```

- **`F_GETFL` é proibido pelo subject**, então setamos `O_NONBLOCK` direto, sem ler
  as flags antes ([:58](../../src/common/Socket.cpp#L58)).
- **`inet_addr`/`inet_pton` não estão na lista de funções autorizadas** — o host é
  convertido por `StringUtils::parseIPv4()` (M2) e passado por `htonl`.
- `strerror(errno)` depois de `socket`/`bind`/`listen`/`fcntl` é permitido: são
  erros de inicialização, não de I/O.

**`ListeningSocket::onReadable()`** ([src/core/Server.cpp:31](../../src/core/Server.cpp#L31))
faz `accept()` em laço até `-1` — uma notificação do `poll()` pode representar
várias conexões pendentes. `Socket::acceptConnection()` devolve `-1` e o laço para;
**não checamos `errno`** (fila vazia e falha real têm o mesmo desfecho: parar).
O FD aceito também vira não-bloqueante antes de virar `Client`.

**`Server::start()`** ([:66](../../src/core/Server.cpp#L66)) agrupa os `ServerConfig`
por `(host, porta)` num `std::map` e cria **um** `ListeningSocket` por endpoint,
passando o vetor de vhosts daquele endpoint. É isso que permite dois `server {}`
na mesma porta sem conflito de `bind`.

---

## 3. `Client` — a máquina de estados

[src/core/Client.cpp](../../src/core/Client.cpp)

```
             ┌──────────────── keep-alive ────────────────┐
             ▼                                            │
     READING_HEADERS ──► (dispatch) ──► WRITING_RESPONSE ─┴─► DONE
             │                                ▲
             └──► WAITING_CGI ────────────────┘
                  (CgiHandler no poll)
```

### Leitura ([:57](../../src/core/Client.cpp#L57))

```cpp
ssize_t ret = recv(fd_.get(), buffer, sizeof(buffer), 0);
if (ret <= 0) { wantsClose_ = true; state_ = DONE; return; }  // 0 = EOF, -1 = erro
```

Um `if` cobre os dois casos que a régua exige. Nenhum `errno` depois.

### Escrita ([:84](../../src/core/Client.cpp#L84))

```cpp
ssize_t bytes_sent = send(fd_.get(), outBuffer_.c_str() + outOffset_, remaining, 0);
if (bytes_sent < 0) { wantsClose_ = true; state_ = DONE; return; }
outOffset_ += bytes_sent;
if (outOffset_ < outBuffer_.size()) return;   // envio parcial: volta ao poll()
```

**Envio parcial é o caso normal** — o buffer do kernel enche e `send` devolve menos
bytes do que pedimos. Guardamos `outOffset_` e continuamos no próximo `POLLOUT`.
Se `send` devolver `0`, o offset simplesmente não avança e esperamos o próximo
evento; não é erro, e como voltamos ao `poll()` não há busy-loop.

### keep-alive e pipelining

Terminado o envio, se `request_.keepAlive()` for verdadeiro voltamos a
`READING_HEADERS`. Antes disso, `tryConsumeResidual()`
([:230](../../src/core/Client.cpp#L230)) alimenta o parser com zero bytes: se a
requisição seguinte já veio no mesmo `recv()` (pipelining), ela é processada sem
esperar novos dados. O header `Connection` é escrito pelo servidor no momento da
serialização ([:90](../../src/core/Client.cpp#L90)) — quem manda na semântica da
conexão é o servidor, não o handler nem o script CGI.

### Timeout ([:218](../../src/core/Client.cpp#L218))

60 s sem atividade → 408 + fecha. Conexões esperando CGI são puladas (o
`CgiHandler` tem timeout próprio, de 10 s).

### Virtual host e limite de body

`matchVirtualHost()` ([:137](../../src/core/Client.cpp#L137)) compara o `Host` (sem
a porta) com os `server_name`; sem match, o primeiro bloco do endpoint.

`feedParser()` ([:166](../../src/core/Client.cpp#L166)) é a parte sutil: o parser
devolve `HEADERS_READY` ao terminar os headers, porque o limite de body depende da
location — que só é conhecida depois de ler `Host` e path. Aí recalculamos com
`effectiveBodyLimit()` e retomamos o parse. Fazer isso antes era o bug que fazia a
location perder para o valor do server.

---

## 4. `CgiHandler` — `fork()` sem bloquear

[src/cgi/CgiHandler.cpp](../../src/cgi/CgiHandler.cpp) · **o único `fork()` do projeto** ([:98](../../src/cgi/CgiHandler.cpp#L98))

```
pipe(in) ; pipe(out) ; fork()
 ├── filho  : dup2(in[0]→stdin), dup2(out[1]→stdout), fecha as 4 pontas,
 │            chdir(diretório do script), execve(interpretador, script, envp)
 └── pai    : fica com in[1] (escrita) e out[0] (leitura), ambos O_NONBLOCK,
              loop.add(this)  → entra no poll() como mais um IPollable
```

**Fases** (`fd()` e `interest()` mudam conforme a fase):

| Fase | FD observado | Evento | O que faz |
|---|---|---|---|
| `WRITING_INPUT` | `stdinPipe_` | `POLLOUT` | escreve o body em blocos de 4 KB ([:146](../../src/cgi/CgiHandler.cpp#L146)) |
| `READING_OUTPUT` | `stdoutPipe_` | `POLLIN` | acumula o stdout ([:181](../../src/cgi/CgiHandler.cpp#L181)) |
| `FINISHED` | — | `0` | resposta entregue; `wantsClose()` → `reapClosed()` deleta |

**Detalhes que valem ponto:**

- **`chdir` antes do `execve`** ([:68](../../src/cgi/CgiHandler.cpp#L68)): a régua
  exige que o CGI rode no diretório correto para caminhos relativos. O `argv[1]` é
  só o nome do arquivo, já que o cwd é o diretório dele.
- **O filho fecha as quatro pontas originais** ([:62](../../src/cgi/CgiHandler.cpp#L62)):
  se ele mantivesse a ponta de escrita do próprio stdin, nunca veria EOF.
- **EOF é o fim do body** (subject): terminado o envio, `stopWritingInput()` fecha
  `stdinPipe_` — é o fechamento que sinaliza EOF ao script. Sem body, fechamos já no
  `start()`. Requisições `chunked` chegam ao script já desmontadas, porque o parser
  entrega `req.body()` pronto.
- **EOF também marca o fim da saída**: lemos até `read()` devolver `<= 0`; aí
  `ResponseFactory::makeFromCgi` monta a resposta. Se o script não mandou
  `Content-Length`, ele é calculado por `setBody()`.
- **`POLLHUP` no stdout** ([:190](../../src/cgi/CgiHandler.cpp#L190)) chega junto
  com o que ainda está no buffer do kernel: fazemos uma leitura por evento e só
  encerramos quando ela devolver 0 — senão perderíamos a saída de scripts rápidos.
- **Timeout de 10 s** ([:203](../../src/cgi/CgiHandler.cpp#L203)) → 504. `loop.py`
  (laço infinito) demonstra.
- **`reapChild()`** ([:235](../../src/cgi/CgiHandler.cpp#L235)): `waitpid(WNOHANG)`;
  se o filho ainda estiver vivo, `SIGKILL` + `waitpid` bloqueante (que retorna na
  hora, porque `SIGKILL` não pode ser ignorado). Nenhum zumbi sobrevive ao siege.
- **Cliente que desiste no meio**: `~Client` chama `detachClient()`, que mata o
  script e zera o ponteiro — o `CgiHandler` nunca escreve numa `Request` destruída
  ([Client.cpp:33](../../src/core/Client.cpp#L33)).

---

## 5. RAII de file descriptors

[FileDescriptor](../../src/common/FileDescriptor.cpp) fecha no destrutor, cópia
desabilitada, transferência de posse por `release()`. Ninguém chama `close()` na mão.

Isso é o que faz `valgrind --track-fds=yes` fechar em zero, inclusive nos caminhos
de erro: em `CgiHandler::start()`, se o segundo `pipe()` ou o `fork()` falharem, os
FDs já criados são embrulhados em `FileDescriptor` locais e fecham ao sair do escopo
([CgiHandler.cpp:94](../../src/cgi/CgiHandler.cpp#L94), [:101](../../src/cgi/CgiHandler.cpp#L101)).

---

## 6. Perguntas que vão cair em cima de você

**"Mostre o caminho do `poll()` até o I/O do cliente."**
`EventLoop::runOnce` → `revents & POLLIN` → `Client::onReadable()` → `recv()`.
Não existe outro caminho: `recv`/`send` aparecem só nessas duas funções.

**"O que acontece se `recv` devolver -1?"**
Fechamos a conexão (`wantsClose_`), sem olhar `errno`. Como o FD é não-bloqueante e
só lemos após o `poll()` avisar, `EAGAIN` é um caso que essencialmente não ocorre —
e mesmo que ocorresse, fechar é seguro e é o que a régua pede.

**"Por que o `Client` não se deleta sozinho no `onHangup()`?"**
Porque o `EventLoop` está iterando sobre o vetor; apagar ali invalidaria o iterador
e derrubaria o servidor. Marcamos e deixamos `reapClosed()` limpar depois.

**"Onde está o `SIGPIPE`?"**
Ignorado no [main.cpp:23](../../src/main.cpp#L23). Escrever num socket ou pipe já
fechado não pode matar o processo — o `-1` do `send`/`write` já é tratado.

**"Ctrl-C vaza alguma coisa?"**
Não. O handler só seta `g_shutdown`; o loop sai, o destrutor do `EventLoop` deleta
os `IPollable*` e cada `FileDescriptor` fecha o seu FD.
