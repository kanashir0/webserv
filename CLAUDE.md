# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projeto

Servidor HTTP/1.1 **single-thread, non-blocking** desenvolvido para o projeto webserv da 42 School. Todo o I/O multiplexing usa exclusivamente `poll()`.

## Comandos

```bash
make            # compila → binário ./webserv
make re         # recompila do zero
make clean      # remove build/
make fclean     # remove build/ e ./webserv
make test       # compila e roda tests/scripts/curl-suite.sh

./webserv conf/default.conf          # executa com config padrão (demo completa)
./webserv conf/valid/basic.conf      # config mínima
./webserv conf/invalid/qualquer.conf # deve falhar no startup com [ERROR] arquivo:linha:

tests/scripts/run-siege.sh           # stress test (requer siege instalado)
tests/scripts/run-valgrind.sh conf/default.conf   # detecta leaks de memória e FD
```

## Flags de compilação obrigatórias

```
c++ -Wall -Wextra -Werror -std=c++98 -pedantic -Iinclude
```

Nunca suprimir warnings. Parâmetros de funções stub são comentados (`/* param */`) propositalmente para satisfazer `-Wunused-parameter` sem alterar a assinatura.

## Restrições críticas do subject

| Restrição | Onde se aplica |
|-----------|---------------|
| **`poll()` chamado apenas em um ponto** | Somente em `EventLoop::runOnce()` |
| **Nunca `read()`/`write()` sem `poll()` indicar** | Apenas dentro de `onReadable()`/`onWritable()` |
| **Proibido checar `errno` após `read`/`recv`/`write`/`send`** | `recv`/`send` retornando `-1` fecham a conexão — **nunca** testar `EAGAIN`/`EWOULDBLOCK`. `strerror(errno)` segue permitido em `socket`/`bind`/`listen`/`fcntl` |
| **`fork()` somente para CGI** | Somente em `CgiHandler::start()` |
| **Sem threads** | Todo o projeto é single-thread |
| **`fcntl()` só com `F_SETFL`, `O_NONBLOCK`, `FD_CLOEXEC`** | `Socket::setNonBlocking()` e pipes do CGI — **`F_GETFL` é proibido**, use `fcntl(fd, F_SETFL, O_NONBLOCK)` direto |
| **Só as funções externas da lista do subject** | `inet_pton`/`inet_addr` **não** estão na lista — use `StringUtils::parseIPv4()` |
| **C++98 strict** | Sem `nullptr`, sem `auto`, sem range-for, sem `std::to_string`, sem smart pointers |

## Arquitetura

### Padrão Reactor

O coração do servidor: `EventLoop` mantém uma lista de `IPollable*`, constrói o array `pollfd[]`, chama `poll()` uma vez, e despacha `onReadable()`/`onWritable()`/`onHangup()` apenas para FDs prontos. Nenhum outro arquivo chama `poll()` diretamente.

```
main() → ConfigParser → Server → EventLoop::run()
                          │
                    [poll() loop]
                    ├── ListeningSocket (IPollable) → accept() → new Client
                    ├── Client (IPollable) → RequestParser → Router → Response
                    └── CgiHandler (IPollable) → pipe de stdin/stdout do script
```

### Fluxo de uma requisição

1. `ListeningSocket::onReadable()` — `accept()` → cria `Client`, registra no `EventLoop`
2. `Client::onReadable()` — `recv()` → `RequestParser::feed()` → `FeedResult::COMPLETE`
3. `Router::route(req, vhost)` — `findLocation()` → `IMethodHandler::handle()`
4. `GetHandler`/`PostHandler`/`DeleteHandler` → `ResponseFactory` → `Response`
5. `Client::onWritable()` — `send()` com offset (envio parcial é normal)
6. `EventLoop::reapClosed()` — deleta `Client`s com `wantsClose()==true`

### State machine do `Client`

`READING_HEADERS` → (`WAITING_CGI`) → `WRITING_RESPONSE` → `DONE`

O método `interest()` retorna `POLLIN` em `READING_HEADERS`, `POLLOUT` em
`WRITING_RESPONSE` e `0` nos demais — o `EventLoop` consulta isso a cada
iteração. Com `interest() == 0` o `poll()` ainda reporta `POLLHUP`/`POLLERR`, que
é como uma desconexão durante o CGI é detectada.

### Limite de body: o parser pausa em `HEADERS_READY`

`client_max_body_size` pode ser sobrescrito por location, mas a location só é
conhecida depois de ler os headers. Por isso `RequestParser::feed()` devolve
`HEADERS_READY` ao terminar os headers quando há body pela frente; o
`Client::feedParser()` recalcula o limite com `effectiveBodyLimit()` e retoma o
parse. **Nunca** calcule o limite antes do `feed()` — era exatamente esse o bug
que fazia a location perder para o valor do server.

## Estrutura de módulos

| Diretório | Responsabilidade | Dono |
|-----------|-----------------|------|
| `src/common/` | `FileDescriptor` (RAII), `Socket`, `Logger` (Singleton), `StringUtils`, `HttpStatus`, `Types` | M2 |
| `src/config/` | `ConfigParser` (parser estilo Nginx), `ServerConfig`, `LocationConfig` | M2 |
| `src/core/` | `EventLoop` (único `poll()`), `Server`, `ListeningSocket`, `Client` | M1 |
| `src/http/` | `RequestParser`, `Request`/`Response` (DTOs), `Router`, handlers GET/POST/DELETE, `ResponseFactory`, `MimeTypes` | M2/M3 |
| `src/cgi/` | `CgiHandler` (fork/exec não-bloqueante via pipes), `CgiEnv` (variáveis RFC 3875) | M1/M3 |
| `src/session/` | `Session`, `SessionStore` (GC por TTL, `gc()` chamado a cada tick do loop) | M3 |
| `include/` | Headers espelhando a mesma estrutura de `src/` | — |

Headers ficam em `include/<modulo>/`, sources em `src/<modulo>/`. O Makefile usa `-Iinclude` e encontra tudo automaticamente via `find`.

## Convenções importantes

### RAII obrigatório para FDs
`FileDescriptor` fecha o FD no destrutor. Nunca chamar `close()` manualmente. A cópia de `FileDescriptor` é desabilitada (private) — transferir ownership via `release()`.

### `Request` é imutável após parsing
Campos privados; apenas `RequestParser` (declarado `friend`) escreve. Handlers só lêem via getters `const`.

### `HeaderMap` é case-insensitive
`typedef std::map<std::string, std::string, CaseInsensitiveLess> HeaderMap` — conforme RFC 7230, `Content-Length` e `content-length` são equivalentes.

### `Response::setBody()` atualiza `Content-Length` automaticamente
Nunca setar `Content-Length` manualmente após `setBody()`.

### Remoção de `IPollable` do loop
Nunca deletar dentro de `onHangup()` (iterator invalidation). Setar `wantsClose_ = true`; o `reapClosed()` deleta após a iteração do `poll()`.

## Configuração (estilo Nginx)

```nginx
server {
    listen        8080;
    server_name   localhost;
    root          ./www;
    index         index.html;
    client_max_body_size 1m;   # default; pode ser sobrescrito por location

    error_page 404 /errors/404.html;

    location / {
        methods GET;
        autoindex off;
    }

    location /upload {
        methods POST DELETE;
        upload_store ./www/uploads;
        client_max_body_size 10m;
    }

    location /cgi-bin {
        methods GET POST;
        root ./cgi-bin;
        cgi .py /usr/bin/python3;
    }

    location /old {
        return 301 /;   # a diretiva é `return` (estilo Nginx), não `redirect`;
    }                   # o campo do struct é que se chama LocationConfig::redirect
}
```

Validações que falham já no startup (`[ERROR] arquivo.conf:linha: mensagem`): porta fora
de 1–65535, host que não seja IPv4 válido, `methods` diferente de GET/POST/DELETE,
`autoindex` diferente de on/off, `return` com código diferente de 301/302 ou sem destino,
`root`/`upload_store` apontando para diretório inexistente, `server_name` fora do charset
de hostname, diretiva duplicada no mesmo bloco, `location` com path repetido, extensão
`cgi` repetida, e `.conf` sem nenhum bloco `server`.

Cada regra tem um arquivo correspondente em `conf/invalid/` — ao adicionar uma
validação nova, adicione também o `.conf` que a dispara.

### Herança server → location

| Diretiva | server | location | Como a location herda |
|----------|--------|----------|----------------------|
| `root`, `index` | ✅ | ✅ | string vazia na location = herda |
| `autoindex` | ✅ | ✅ | `autoindexSet == false` = herda |
| `client_max_body_size` | ✅ | ✅ | `clientMaxBodySizeSet == false` = herda |
| `error_page` | ✅ | ✅ | mapa vazio (ou código ausente) = herda |
| `listen`, `server_name` | ✅ | ❌ | — |
| `methods`, `return`, `cgi`, `upload_store` | ❌ | ✅ | — |

`false` e `0` são valores legítimos, então quem não tem sentinela natural carrega
um booleano `...Set`. **`client_max_body_size 0` significa "sem limite" nos dois
níveis** — igual ao que o `RequestParser` faz com `maxBody == 0`. Não reintroduza
a semântica antiga de "0 = herda": ela tornava impossível configurar ilimitado.

A `error_page` da location chega ao `ResponseFactory` pelo parâmetro opcional
`const LocationConfig* loc` de `makeError`/`makeFile`/`makeAutoindex`/`makeFromCgi`.
Passe `&loc` sempre que houver uma location resolvida; `0` só quando genuinamente
não há (404 sem location casada, erro de parsing, catch-all de exceção).

- `findLocation()` usa **longest-prefix match** (comportamento Nginx)
- Virtual hosting: múltiplos `server {}` na mesma porta compartilham um único `ListeningSocket`; o `Client` seleciona o vhost pelo header `Host`

## Dívida técnica conhecida

- **`ConfigParser` usa cadeia de `if/else if`** para despachar diretivas, em vez
  de uma tabela `std::map<std::string, Setter>` com ponteiro-para-membro (como o
  projeto de referência 42-webserv). Avaliado e adiado deliberadamente antes da
  entrega: são ~14 funções-membro novas mais dois mapas, contra 141 linhas que
  funcionam e têm 26 configs inválidos cobrindo cada validação. Ganho funcional
  zero. Só vale mexer depois da defesa.

## Branches de desenvolvimento

- `main` — protegida, só via PR com squash merge
- `feat/core-network` — M1 (EventLoop, Server, Client, Socket, CgiHandler)
- `feat/parsers` — M2 (ConfigParser, RequestParser, common/)
- `feat/http-logic` — M3 (Router, handlers, ResponseFactory, session/)

## Testes manuais essenciais

```bash
curl http://localhost:8080/                          # 200
curl http://localhost:8080/__nope__                  # 404
curl -X PUT http://localhost:8080/                   # 405
curl --data-binary @big.bin http://localhost:8080/upload  # 413 se acima do limite
curl http://localhost:8080/ -H "Host: site-b.local"  # virtual hosting
```

Após qualquer execução, verificar com `valgrind --track-fds=yes` que não há FDs abertos nem bytes perdidos.
