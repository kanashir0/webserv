# Guia de defesa — visão macro

> Documento principal para a avaliação. Leia este antes da defesa; depois cada um
> revisa o seu: [Membro 1](membro-1-core-network.md) · [Membro 2](membro-2-parsers.md) · [Membro 3](membro-3-http-logic.md).

---

## 1. O que o servidor é, em três frases

Servidor HTTP/1.1 em C++98, sem bibliotecas externas, **um processo, uma thread,
um único `poll()`**. Todo o comportamento vem de um arquivo de configuração
estilo Nginx: portas, rotas, métodos, uploads, CGI, páginas de erro e limites de
body. O único `fork()` do projeto existe para executar scripts CGI.

**Regra que resume o design:** nenhum `recv`/`send`/`read`/`write` acontece fora de
um callback disparado pelo `poll()`.

---

## 2. Arquitetura (padrão Reactor)

```
main.cpp  →  ConfigParser  →  vector<ServerConfig>
                │
             Server::start()  →  agrupa configs por (host, porta)
                │                 1 ListeningSocket por endpoint
                ▼
        ╔══════ EventLoop::runOnce() ══════╗   ← ÚNICO poll() do projeto
        ║  monta pollfd[] com interest()   ║
        ║  poll(&fds[0], n, 1000)          ║
        ║  despacha por revents            ║
        ╚══════════════╦═══════════════════╝
                       │
      ┌───────────────┼───────────────┬────────────────┐
      ▼               ▼               ▼                ▼
ListeningSocket     Client        CgiHandler     CgiStdinPump
accept() → Client  recv/send      read do stdout  write no stdin
                   parser+router  do script       do script
```

O CGI ocupa **dois** pollables porque são dois FDs, e a interface cobre um cada.

Qualquer objeto com um file descriptor implementa `IPollable`
([include/core/IPollable.hpp](../../include/core/IPollable.hpp)):

```cpp
int   fd() const;          // qual FD monitorar
short interest() const;    // POLLIN, POLLOUT ou 0
void  onReadable();        // poll disse: dá pra ler
void  onWritable();        // poll disse: dá pra escrever
void  onHangup();          // POLLHUP/POLLERR
bool  wantsClose() const;  // "pode me deletar" (o loop deleta depois da iteração)
void  checkTimeout(now, timeout);
```

O `EventLoop` guarda `std::vector<IPollable*>`, é dono da memória e deleta tudo no
destrutor ([src/core/EventLoop.cpp:4](../../src/core/EventLoop.cpp#L4)).

---

## 3. Ciclo de vida de uma requisição

```
1. ListeningSocket::onReadable()  → accept() em laço → new Client → loop.add()
2. Client::onReadable()           → recv() → RequestParser::feed()
3. FeedResult::COMPLETE           → Client::dispatch()
4. Router::route(req, vhost, cgi) → findLocation → método → redirect → handler
5. GetHandler/PostHandler/DeleteHandler → ResponseFactory → Response
   (ou: CgiHandler entra no poll() e devolve depois via onCgiComplete())
6. Client::onWritable()           → send() com offset (envio parcial é normal)
7. keep-alive? volta a READING_HEADERS : wantsClose_ = true
8. EventLoop::reapClosed()        → delete dos que pediram para fechar
```

State machine do `Client` ([include/core/Client.hpp:25](../../include/core/Client.hpp#L25)):

```
READING_HEADERS ──(CGI)──► WAITING_CGI ──► WRITING_RESPONSE ──► DONE
        └──────────────────────────────────────►┘
                    ▲                                 │
                    └────────── keep-alive ───────────┘
```

`interest()` devolve `POLLIN` em READING_HEADERS, `POLLOUT` em WRITING_RESPONSE e
`0` nos demais — é assim que o loop sabe o que monitorar em cada FD.

---

## 4. Régua → onde está no código

| Item da régua | Onde | Como mostrar |
|---|---|---|
| Um único `poll()` | [EventLoop.cpp:35](../../src/core/EventLoop.cpp#L35) | `grep -rn "poll(" src/` → uma só chamada (as outras linhas são comentários) |
| `poll()` no laço principal, leitura **e** escrita ao mesmo tempo | [EventLoop.cpp:17-61](../../src/core/EventLoop.cpp#L17-L61) | `fds[i].events = p->interest()`; num mesmo array há FDs com POLLIN e com POLLOUT |
| I/O só após readiness | [Client.cpp:62](../../src/core/Client.cpp#L62) e [:98](../../src/core/Client.cpp#L98) | `recv` só em `onReadable`, `send` só em `onWritable` |
| Erro em `recv`/`send` remove o cliente | [Client.cpp:64-68](../../src/core/Client.cpp#L64-L68), [:100-104](../../src/core/Client.cpp#L100-L104) | `wantsClose_ = true; state_ = DONE` |
| Retorno de `recv` tratado (0 **e** -1) | [Client.cpp:64](../../src/core/Client.cpp#L64) | `if (ret <= 0)` cobre EOF e erro |
| **Nunca** `errno` após I/O | — | `grep -rn errno src/` → só `socket/bind/listen/fcntl` e `poll` |
| `fork()` só para CGI | [CgiHandler.cpp:98](../../src/cgi/CgiHandler.cpp#L98) | `grep -rn "fork(" src/` → uma só chamada (a outra linha é um log) |
| `fcntl` só `F_SETFL`/`O_NONBLOCK` | [Socket.cpp:58](../../src/common/Socket.cpp#L58), [CgiHandler.cpp:114](../../src/cgi/CgiHandler.cpp#L114) | sem `F_GETFL` em lugar nenhum |
| Sem threads | — | `grep -rn pthread src/` → vazio |
| Compila sem religação | [Makefile](../../Makefile) | `make` duas vezes: a segunda não recompila |
| Páginas de erro padrão | [ResponseFactory.cpp:105](../../src/http/ResponseFactory.cpp#L105) | comentar `error_page` no .conf e ver a página embutida |
| Status codes corretos | [HttpStatus.cpp](../../src/common/HttpStatus.cpp) | tabela da seção 6 |
| Múltiplos interface:porta | [Server.cpp:66](../../src/core/Server.cpp#L66) | `conf/default.conf` tem 8080 e 8081 em interfaces diferentes |
| Vários sites no mesmo ip:porta | [Client.cpp:137](../../src/core/Client.cpp#L137) | virtual host pelo header `Host` |
| Limite de body do cliente | [Client.cpp:155](../../src/core/Client.cpp#L155) + [RequestParser.cpp:244](../../src/http/RequestParser.cpp#L244) | 413 no server (1m) e na location (10m) |
| Rotas para diretórios diferentes | [PathResolver.cpp:125](../../src/http/PathResolver.cpp#L125) | `root` por location |
| Arquivo default de diretório | [GetHandler.cpp:48](../../src/http/handlers/GetHandler.cpp#L48) | `index index.html` |
| Métodos aceitos por rota | [Router.cpp:137](../../src/http/Router.cpp#L137) | 405 + header `Allow` |
| GET / POST / DELETE | `src/http/handlers/` | seção 5 |
| Método desconhecido não derruba | [Router.cpp:92](../../src/http/Router.cpp#L92) | `curl -X PUT` → 405 |
| Upload de arquivos | [PostHandler.cpp:159](../../src/http/handlers/PostHandler.cpp#L159) | `upload_store` |
| CGI roda no diretório certo | [CgiHandler.cpp:68](../../src/cgi/CgiHandler.cpp#L68) | `chdir(directoryOf(script))` antes do `execve` |
| CGI com erro / loop infinito | [CgiHandler.cpp:191](../../src/cgi/CgiHandler.cpp#L191) | 502 e 504, servidor continua vivo |
| Siege > 99.5% | [tests/scripts/run-siege.sh](../../tests/scripts/run-siege.sh) | seção 7 |
| Sem leaks de memória e de FD | [tests/scripts/run-valgrind.sh](../../tests/scripts/run-valgrind.sh) | `--track-fds=yes` |

---

## 5. Demonstração ao vivo (roteiro de 5 minutos)

```bash
make re                                   # zero warnings
./webserv conf/default.conf               # 3 sites: 8080 (x2 vhosts) e 8081

curl -i http://127.0.0.1:8080/                       # 200 estático
curl -i http://127.0.0.1:8080/__nope__               # 404 (error_page do server)
curl -i http://127.0.0.1:8080/upload/nope            # 404 (error_page da location)
curl -i -X PUT http://127.0.0.1:8080/                # 405 + Allow: GET
curl -i http://127.0.0.1:8080/old                    # 301 → /
curl -i http://127.0.0.1:8080/files/                 # autoindex on
curl -i http://127.0.0.1:8080/errors/                # 404: autoindex off, sem index

# upload + download + delete
curl -i -X POST --data-binary @arquivo.txt http://127.0.0.1:8080/upload/arquivo.txt
curl -i http://127.0.0.1:8080/upload/arquivo.txt
curl -i -X DELETE http://127.0.0.1:8080/upload/arquivo.txt      # 204

# limite de body: server 1m, location /upload 10m
head -c 2000000 /dev/zero | tr '\0' A | curl -si -X POST --data-binary @- http://127.0.0.1:8080/        # 413
head -c 2000000 /dev/zero | tr '\0' A | curl -si -X POST --data-binary @- http://127.0.0.1:8080/upload/ok.bin  # 201

# CGI
curl -i http://127.0.0.1:8080/cgi-bin/hello.py                  # 200 GET
curl -i -X POST -d "campo=valor" http://127.0.0.1:8080/cgi-bin/hello.py   # 200 POST
curl -i http://127.0.0.1:8080/cgi-bin/broken.py                 # 502 (script morre)
curl -i http://127.0.0.1:8080/cgi-bin/loop.py                   # 504 após 10s
curl -i http://127.0.0.1:8080/                                  # servidor continua vivo

# CGI: "a requisição completa e os argumentos do cliente estão disponíveis"
curl -i "http://127.0.0.1:8080/cgi-bin/env_dump.py?a=1&b=2"     # QUERY_STRING + headers HTTP_*
curl -i -X POST --data-binary "corpo" http://127.0.0.1:8080/cgi-bin/post_echo.py   # body no stdin
# chunked: o parser desmonta antes de entregar, o script recebe o body inteiro
curl -i -X POST -H "Transfer-Encoding: chunked" -H "Expect:" \
     --data-binary "corpo-chunked" http://127.0.0.1:8080/cgi-bin/post_echo.py

# CGI: "executado no diretório correto para acesso a arquivos de caminho relativo"
curl -i http://127.0.0.1:8080/cgi-bin/relative.py               # CWD=.../cgi-bin + conteúdo de data.txt

# virtual host e segunda interface
curl -i -H "Host: site-b.local" http://127.0.0.1:8080/
curl -i http://127.0.0.2:8081/

# requisições malformadas (telnet/nc): nada de crash
printf 'GARBAGE\r\n\r\n'                | nc 127.0.0.1 8080     # 400
printf 'GET / HTTP/9.9\r\nHost: x\r\n\r\n' | nc 127.0.0.1 8080  # 505
printf 'GET / HTTP/1.1\r\n\r\n'         | nc 127.0.0.1 8080     # 400 (sem Host)
printf 'POST /upload/x HTTP/1.1\r\nHost: x\r\n\r\n' | nc 127.0.0.1 8080  # 411

# bônus: sessão/cookie
curl -c jar http://127.0.0.1:8080/session && curl -b jar http://127.0.0.1:8080/session
```

Configs inválidas — cada arquivo de `conf/invalid/` dispara **uma** validação:

```bash
./webserv conf/invalid/invalid_port.conf
# [ERROR] conf/invalid/invalid_port.conf:3: port out of range: '-5'
```

No navegador: abrir a aba Network, ver request/response headers, seguir o 301,
listar `/files/`, e testar a URL errada.

---

## 6. Status codes que o servidor emite

| Código | Quando | Onde |
|---|---|---|
| 200 | arquivo, autoindex, CGI ok | `ResponseFactory::makeFile/makeAutoindex/makeFromCgi` |
| 201 | upload gravado | `PostHandler` |
| 204 | DELETE bem-sucedido | `DeleteHandler` |
| 301 / 302 | `return` da location; diretório sem `/` final | `makeRedirect`, `GetHandler` |
| 400 | request line/headers inválidos, sem `Host`, `Content-Length` duplicado, percent-encoding inválido | `RequestParser`, `PathResolver` |
| 403 | sem permissão, path traversal, POST sem `upload_store` | `GetHandler`, `PathResolver`, `PostHandler` |
| 404 | arquivo/location inexistente; diretório sem index e sem autoindex | `Router`, handlers |
| 405 | método fora de `methods` (com header `Allow`) | `Router::route` |
| 408 | conexão ociosa por 60s | `Client::checkTimeout` |
| 411 | POST sem `Content-Length` nem `chunked` | `RequestParser::parseHeaders` |
| 413 | body acima de `client_max_body_size` | `RequestParser` |
| 414 | URI acima de 8192 bytes | `RequestParser::parseRequestLine` |
| 500 | erro interno (exceção, root ausente) | `Router`, `ResponseFactory` |
| 502 | CGI sem headers válidos ou que morreu sem escrever (vira 404 se o script não existe) | `makeFromCgi`, `CgiHandler::buildResponse` |
| 504 | CGI ultrapassou 10s | `CgiHandler::checkTimeout` |
| 505 | versão HTTP diferente de 1.0/1.1 | `RequestParser` |

---

## 7. Testes

```bash
make test                                        # 38 checks de curl (PASS/FAIL)
tests/scripts/run-siege.sh                       # -b, 50 clientes, 30s, exige > 99.5%
tests/scripts/run-valgrind.sh conf/default.conf  # leaks de memória E de FD
```

Estado atual: `make re` sem warnings, `make test` 38/38, valgrind com 0 bytes
perdidos e 0 FDs vazados, siege acima de 99.5%.

O script de siege mistura estático, 404 e CGI de propósito: só bater em `/` não
exercita o `fork()`, que é onde um vazamento de FD ou de processo zumbi apareceria.

---

## 8. Perguntas prováveis e respostas curtas

**O que é o HTTP, em resumo?**
Protocolo texto de requisição/resposta sobre TCP. O cliente manda
`MÉTODO URI VERSÃO`, headers, linha em branco e (opcionalmente) body; o servidor
responde `VERSÃO STATUS RAZÃO`, headers, linha em branco e body.

**Qual mecanismo de eventos vocês usam?**
`poll()`, num só ponto: [EventLoop.cpp:35](../../src/core/EventLoop.cpp#L35).

**Como o `poll()` funciona?**
Recebe um array de `pollfd` (`fd`, `events` desejados) e bloqueia até que algum FD
fique pronto ou o timeout de 1000 ms expire. Ele preenche `revents` com o que de
fato aconteceu (`POLLIN`, `POLLOUT`, `POLLHUP`, `POLLERR`) e nós despachamos só
para os FDs marcados. É a inversão de controle do padrão Reactor: em vez de
perguntar "esse socket tem dados?" um a um, o kernel diz quais têm.

**Como o mesmo `poll()` atende `accept()` e o I/O dos clientes?**
Os três tipos de objeto (`ListeningSocket`, `Client`, `CgiHandler`) implementam
`IPollable` e vivem na mesma lista. O listener pede `POLLIN` sempre; o cliente pede
`POLLIN` enquanto lê e `POLLOUT` enquanto escreve. Leitura e escrita são
monitoradas na mesma chamada, em FDs diferentes.

**E a desconexão do cliente?**
`recv`/`send` devolvendo `<= 0` marca `wantsClose_`, e `POLLHUP`/`POLLERR` chamam
`onHangup()`, que faz o mesmo. O objeto nunca é deletado dentro do callback (isso
invalidaria o iterador); `reapClosed()` deleta depois da iteração
([EventLoop.cpp:80](../../src/core/EventLoop.cpp#L80)).

**Vocês checam `errno` depois de ler/escrever?**
Não. `recv`/`send`/`read`/`write` retornando erro fecham a conexão, sem consultar
`errno`. Ele só aparece depois de `socket`/`bind`/`listen`/`fcntl` (erros de
inicialização, permitido) e depois de `poll()`, para distinguir `EINTR`.

**O CGI não bloqueia o servidor?**
Não. O pai fica com a ponta de escrita do stdin e a de leitura do stdout do filho,
ambas `O_NONBLOCK`, e cada uma vira um `IPollable`: `CgiHandler` lê o stdout,
`CgiStdinPump` escreve o body. Enquanto o script roda, o servidor atende outros
clientes normalmente. Timeout de 10s → `SIGKILL` + 504.

**Por que dois pollables para um script só?**
Porque `IPollable` cobre um FD e os dois precisam ser vigiados ao mesmo tempo. Um
script que ecoa o body enche o próprio stdout, para de ler o stdin e trava quem só
estiver esperando `POLLOUT` — foi exatamente o deadlock que apareceu com um POST de
100 MB. Os dois objetos se soltam por `detachOwner()`/`onStdinClosed()`, porque o
`EventLoop` deleta cada pollable de forma independente.

**Ler o arquivo de configuração e os arquivos estáticos não trava o loop?**
Arquivo regular em disco é exceção explícita da régua, e I/O em disco não retorna
`EAGAIN`. O `.conf` é lido uma vez, antes do loop existir; arquivos estáticos são
lidos com `stat` + uma leitura do tamanho exato
([ResponseFactory.cpp:13](../../src/http/ResponseFactory.cpp#L13)).

**Dois `webserv` na mesma porta ao mesmo tempo?**
O segundo falha no `bind()` e sai com `fatal: Address already in use` e código 1 —
sem crash. Usamos `SO_REUSEADDR` (reaproveita porta em `TIME_WAIT`), não
`SO_REUSEPORT`, então não há dois processos disputando o mesmo endpoint.

**Dois `server {}` no mesmo ip:porta?**
Implementamos virtual host: `Server::start()` agrupa por `(host, porta)`, cria um
único `ListeningSocket` e o `Client` escolhe o bloco pelo header `Host`. Sem match,
vale o primeiro bloco daquele endpoint (mesma regra do Nginx: default server).

---

## 9. Pontos que assumimos e sabemos justificar

- **`send()` retornando 0** não fecha a conexão: significa "nada enviado agora".
  O offset não avança e voltamos ao `poll()`, que avisa quando dá para escrever de
  novo. Erro (`-1`) fecha. Já `recv()` trata `0` (EOF) e `-1` no mesmo `if`.
- **`interest()` devolve 0** enquanto o cliente espera o CGI. O FD continua no
  array do `poll()`, que reporta `POLLHUP`/`POLLERR` mesmo com `events == 0` — é
  assim que detectamos o cliente que desiste no meio do script.
- **Método desconhecido responde 405 + `Allow`**, não 501: a location declara quais
  métodos aceita, então "não permitido aqui" é mais preciso que "não implementado".
- **Timeout de 60s** fecha conexões keep-alive ociosas com 408, para não acumular
  conexões penduradas sob siege.
- **`client_max_body_size 0` significa ilimitado**, nos dois níveis — não "herda".
- **A sessão (bônus) não é criptográfica**: id de 32 hex a partir de relógio,
  contador e `rand()`. Serve para a demonstração, não para autenticação.
- **`PATH_INFO` carrega o caminho da URI**, não o split da RFC 3875 (que aqui seria
  vazio, já que a URI termina no próprio script). `PATH_TRANSLATED` leva o caminho
  em disco. É a convenção que o `cgi_tester` da avaliação exige — sem ela ele
  responde `500 PATH_INFO not found`.
- **O body é bufferizado inteiro em memória** — parser, `Request`, saída do CGI e
  buffer de envio somam ~6 cópias, medidas em 590 MB de pico para um POST de 100 MB.
  Passa no tester, mas é o limite conhecido: streamar a saída do CGI direto para o
  socket é a correção, e ficou fora do escopo da entrega.
- **`ConfigParser` usa cadeia de `if/else if`** em vez de tabela de despacho. Foi
  avaliado e adiado: são ~14 funções novas contra 141 linhas que funcionam e têm 26
  configs inválidas cobrindo cada validação.

---

## 10. Mapa de propriedade do código

| Membro | Branch | Módulos | Documento |
|---|---|---|---|
| **M1** — cbrito-s | `feat/core-network` | `src/core/`, `src/common/Socket`, execução do `src/cgi/CgiHandler` | [membro-1-core-network.md](membro-1-core-network.md) |
| **M2** — gyasuhir | `feat/parsers` | `src/config/`, `src/http/RequestParser`, `src/common/` | [membro-2-parsers.md](membro-2-parsers.md) |
| **M3** — acesar-m | `feat/http-logic` | `src/http/` (Router, handlers, Response, Factory), `src/session/`, `src/cgi/CgiEnv` | [membro-3-http-logic.md](membro-3-http-logic.md) |

Contratos entre os módulos: M2 produz `Request` (imutável, só o parser escreve, via
`friend`), M3 consome `Request` e devolve `Response`, M1 serializa com
`Response::toString()` e envia pelo socket.

**Todo mundo precisa saber explicar a seção 4 inteira** — a régua manda perguntar ao
grupo, não a um membro só.
