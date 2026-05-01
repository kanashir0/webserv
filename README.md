# Documentação de Arquitetura — Webserv

> **Destinatários:** todos os membros do grupo.
> **Objetivo:** entender completamente o que foi gerado no esqueleto antes de começar as implementações.
> **Nota:** métodos marcados com `// TODO Membro X` no `.cpp` são stubs — compilam e linkam, mas não fazem nada útil ainda. Esta documentação explica o que cada um **deve** fazer quando implementado.

---

## Índice

1. [Visão Geral da Arquitetura](#1-visão-geral-da-arquitetura)
2. [Design Patterns — Por que cada um está aqui?](#2-design-patterns--por-que-cada-um-está-aqui)
3. [Mapa de Dependências](#3-mapa-de-dependências)
4. [Fluxo Completo de uma Requisição](#4-fluxo-completo-de-uma-requisição)
5. [Módulo `common/` — Fundação](#5-módulo-common--fundação)
6. [Módulo `config/` — Configuração](#6-módulo-config--configuração)
7. [Módulo `core/` — Motor de Rede](#7-módulo-core--motor-de-rede)
8. [Módulo `http/` — Lógica HTTP](#8-módulo-http--lógica-http)
9. [Módulo `cgi/` — Common Gateway Interface](#9-módulo-cgi--common-gateway-interface)
10. [Módulo `session/` — Sessões e Cookies](#10-módulo-session--sessões-e-cookies)
11. [Divisão de Ownership](#11-divisão-de-ownership)
12. [Restrições do Subject que moldaram a Arquitetura](#12-restrições-do-subject-que-moldaram-a-arquitetura)

---

## 1. Visão Geral da Arquitetura

O webserv é um servidor HTTP/1.1 de **thread única** e **não-bloqueante**. Isso significa que **um único processo** atende a todas as conexões ao mesmo tempo, nunca ficando preso esperando dados chegarem.

A estratégia central é o **I/O Multiplexing com `poll()`**: em vez de criar uma thread por conexão (que travaria no `read()` esperando dados), o servidor pergunta ao sistema operacional "quais sockets têm dados prontos agora?" e só então processa. Se não houver nada pronto, o servidor aguarda com `poll()` por até 1 segundo e tenta novamente — sem jamais bloquear o processo inteiro.

### Componentes principais e suas responsabilidades

```
┌─────────────────────────────────────────────────────────────┐
│                        main.cpp                             │
│  Lê o argv[1] → ConfigParser → Server → EventLoop::run()   │
└─────────────────────────┬───────────────────────────────────┘
                          │ instancia
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                     Server (core/)                          │
│  Agrupa configs por (host,port) → cria ListeningSockets     │
│  Possui: EventLoop, SessionStore, referência ao Router      │
└──────────┬──────────────────────────────────────────────────┘
           │ cria e registra
           ▼
┌──────────────────────────────────────────────────────────────┐
│                   EventLoop (core/)                          │
│  Lista de IPollable* → chama poll() → despacha eventos      │
│  É o ÚNICO lugar onde poll() é chamado (exigência do subject)│
└──┬──────────────────────────────────────────────────────────┘
   │ gerencia
   ├─────────────────────────┐
   ▼                         ▼
ListeningSocket           Client (por conexão)
(IPollable)               (IPollable)
   │                         │
   │ accept() cria           │ onReadable() alimenta
   ▼                         ▼
 Client              RequestParser → Request
                             │
                             │ router_.route()
                             ▼
                          Router
                          │  │  │
                    GetH  │  │  │ PostH  DeleteH
                          ▼  ▼  ▼
                       ResponseFactory → Response
                             │
                             ▼
                      Client.onWritable()
                      └─ send() → socket
```

---

## 2. Design Patterns — Por que cada um está aqui?

### 2.1 Reactor Pattern → `EventLoop` + `IPollable`

**O que é:** um loop central que recebe notificações de I/O do SO e "despacha" cada evento para o objeto correto.

**Por que aqui:**
O subject da 42 proíbe explicitamente fazer `read()` ou `write()` sem que o `poll()` (ou equivalente) indique que o FD está pronto. Sem o Reactor, cada programador correria o risco de chamar `recv()` diretamente, causando bloqueio e zerando a nota.

Com o Reactor, a regra é impossível de violar por descuido: **só o `EventLoop` chama `poll()`**, e só depois de `poll()` retornar é que os métodos `onReadable()`/`onWritable()` são chamados. Nenhum outro arquivo do projeto faz I/O diretamente — eles apenas implementam `IPollable` e o loop cuida do resto.

**Benefício adicional:** o `CgiHandler` (que tem pipes de stdin e stdout) é simplesmente mais um `IPollable`. Ele entra no mesmo `poll()` que os clientes. Isso elimina o `waitpid()` bloqueante que é o erro mais comum em implementações de CGI.

---

### 2.2 State Machine → `Client::State` e `RequestParser::State`

**O que é:** cada objeto mantém uma variável que representa "em que fase estou agora" e só avança para a próxima fase quando a condição for satisfeita.

**Por que aqui:**
Conexões TCP não entregam dados em pacotes completos. Uma requisição HTTP de 200 bytes pode chegar em 3 chamadas de `recv()` separadas: `GET /`, `index.html`, ` HTTP/1.1\r\n...`. Se você reprocessar tudo do zero a cada `recv()`, além de ineficiente, é quase impossível tratar `Transfer-Encoding: chunked` corretamente.

A state machine do `Client` controla o que o `EventLoop` deve observar:
- Em `READING_HEADERS` → monitora apenas `POLLIN` (precisa de dados).
- Em `WRITING_RESPONSE` → monitora apenas `POLLOUT` (precisa mandar dados).
- Em `DONE` → `wantsClose()` retorna `true` → o loop fecha o socket.

Isso é exatamente o que o avaliador da 42 vai perguntar: *"como você sabe quando monitorar leitura vs escrita?"* — a state machine é a resposta.

---

### 2.3 Data Transfer Object (DTO) → `Request`, `Response`, `ServerConfig`

**O que é:** objetos que carregam dados entre módulos sem expor lógica interna.

**Por que aqui:**
São os contratos entre os 3 membros. O Membro 2 produz um `Request`. O Membro 3 consome um `Request` e produz um `Response`. O Membro 1 pega o `Response` e manda pelo socket. Nenhum dos três precisa saber como o outro funciona internamente — basta respeitar a interface do DTO.

`Request` tem campos **privados** com apenas **getters `const`**. Isso é intencional: garante que só o `RequestParser` (declarado `friend`) escreve dentro do `Request`. O Membro 3 não pode acidentalmente modificar um campo e introduzir um bug que parece vir do parser.

---

### 2.4 Strategy → `IMethodHandler`, `GetHandler`, `PostHandler`, `DeleteHandler`

**O que é:** define uma interface para um comportamento (tratar uma requisição HTTP) e permite trocar a implementação concreta sem mudar o código que chama.

**Por que aqui:**
O `Router` recebe uma `Request` e precisa chamar a lógica certa dependendo do método. Sem Strategy, isso viraria um `if/else if/else if` cascateado no `Router::route()`. Com Strategy, o `Router` só chama `handler.handle(req, loc, srv)` e não precisa saber se é GET, POST ou DELETE.

Benefícios práticos para o grupo:
- O Membro 3 implementa `GetHandler`, `PostHandler` e `DeleteHandler` em **arquivos separados** → zero conflito de merge.
- Adicionar HEAD (às vezes pedido no defense) é criar `HeadHandler` sem tocar em nada existente.
- Cada handler tem um arquivo só para ele → fácil de testar com `Request` fake.

---

### 2.5 Factory → `ResponseFactory`

**O que é:** funções que centralizam a criação de objetos complexos.

**Por que aqui:**
Construir uma `Response` de erro correta não é trivial: precisa verificar se existe uma página de erro customizada no `ServerConfig::errorPages`, senão usar uma página embutida, sempre definir `Content-Type`, `Content-Length`, e o status correto. Se cada handler fizer isso do zero, a regra muda em um lugar e quebra em 5.

`ResponseFactory::makeError` centraliza tudo isso. Quando o Membro 3 implementar, todos os handlers herdam o comportamento correto automaticamente.

---

### 2.6 Singleton → `Logger`

**O que é:** garante que existe exatamente uma instância de uma classe em todo o programa, acessível de qualquer lugar.

**Por que aqui:**
O logger precisa ser acessível de `ConfigParser`, `Client`, `CgiHandler`, `Router` — de qualquer lugar do código. A alternativa seria injetar um `Logger&` em todos os construtores de todas as classes, poluindo todas as assinaturas para um componente de I/O lateral.

**Ressalva importante:** o Singleton é justificável aqui porque o `Logger` não tem estado de domínio (não afeta a lógica da requisição). Se um avaliador questionar, a resposta é exatamente essa. Se quiserem remover: `Logger& g_log = Logger::instance()` no `main.cpp` e passam por referência — a mudança é cirúrgica.

---

### 2.7 RAII → `FileDescriptor`, `Socket`

**O que é:** "Resource Acquisition Is Initialization" — um recurso (FD, socket, pipe) é adquirido no construtor e **liberado automaticamente no destrutor**, independente de como o objeto sair do escopo (retorno normal ou exceção).

**Por que aqui:**
Em C++98 sem smart pointers do C++11, gerenciar `close()` manualmente em cada path de erro é a principal fonte de file descriptor leak — que é o principal motivo de reprovação no defense da 42 quando o avaliador roda `valgrind` ou `lsof`.

Exemplo do problema sem RAII:
```cpp
// Sem RAII — leak se accept() falhar depois do socket():
int server_fd = socket(...);
int client_fd = accept(server_fd, ...);
if (client_fd < 0) {
    // Se esquecer o close(server_fd) aqui, leak.
    return;
}
```

Com `FileDescriptor`, o destrutor chama `close()` automaticamente. Mesmo se uma exceção for lançada no meio da função, o FD é fechado.

---

## 3. Mapa de Dependências

Leitura: `A → B` significa "A inclui e usa B".

```
main.cpp
  → ConfigParser        → ServerConfig → LocationConfig → Types
  → Server              → EventLoop → IPollable
                        → Socket    → FileDescriptor
                        → SessionStore → Session
                        → Router    → GetHandler/PostHandler/DeleteHandler
                                    → IMethodHandler
                                    → ResponseFactory → Response
                                    → Request
  → Router (injetado)

Client (IPollable)
  → FileDescriptor
  → RequestParser → Request
  → Response
  → Router
  → SessionStore

CgiHandler (IPollable)
  → FileDescriptor (2 pipes: stdin + stdout)
  → CgiEnv → Request, LocationConfig, ServerConfig
  → Response
  → EventLoop

Logger          ← (nenhuma dependência interna do projeto)
StringUtils     ← Types
HttpStatus      ← (nenhuma dependência interna)
```

**Regra de ouro:** nenhum módulo de "baixo nível" (`common/`) deve incluir módulos de "alto nível" (`http/`, `core/`). As setas de dependência fluem sempre de cima para baixo.

---

## 4. Fluxo Completo de uma Requisição

Este é o roteiro que o código percorrerá quando estiver 100% implementado.

```
1. main()
   ├─ ConfigParser::parseFile("conf/default.conf")
   │    └─ retorna vector<ServerConfig>
   ├─ Router router(configs, sessions)
   ├─ Server server(configs, router)
   ├─ server.start()
   │    └─ para cada (host, port) único nas configs:
   │         └─ new ListeningSocket(host, port, ...)
   │              └─ socket() + bind() + listen() + setNonBlocking()
   │              └─ loop_.add(listeningSocket)
   └─ server.loop().run()          ← começa o loop infinito

2. EventLoop::run() → loop infinito:
   ├─ constrói array de pollfd a partir de pollables_
   ├─ poll(fds, n, 1000)           ← dorme até 1s esperando eventos
   ├─ para cada fd com POLLIN:
   │    └─ pollable->onReadable()
   ├─ para cada fd com POLLOUT:
   │    └─ pollable->onWritable()
   ├─ para cada fd com POLLHUP/POLLERR:
   │    └─ pollable->onHangup()
   └─ reapClosed()                 ← remove e deleta objetos com wantsClose()==true

3. ListeningSocket::onReadable()   ← chega uma nova conexão
   ├─ int client_fd = socket_.accept(addr)
   ├─ new Client(client_fd, vhosts_, router_, sessions_)
   └─ loop_.add(client)            ← cliente entra no mesmo poll()

4. Client::onReadable()            ← chegam bytes do navegador
   ├─ recv(fd_, buf, sizeof(buf))
   ├─ parser_.feed(buf, n, maxBody)
   │    ├─ FeedResult::NEED_MORE   → nada, aguarda mais dados
   │    ├─ FeedResult::COMPLETE    → state_ = ROUTING
   │    └─ FeedResult::BAD_REQUEST → buildErrorResponse(400), state_ = WRITING_RESPONSE
   └─ (se ROUTING) chama router_.route(request_, matchVirtualHost())

5. Router::route(req, vhost)
   ├─ loc = vhost.findLocation(req.path())   ← longest-prefix match
   ├─ se !methodAllowed(req.method(), loc)   → makeError(405, vhost)
   ├─ se loc.redirect não vazio              → makeRedirect(loc.redirect, loc.redirectCode)
   ├─ determina handler pelo método:
   │    ├─ "GET"    → getH_.handle(req, loc, vhost)
   │    ├─ "POST"   → postH_.handle(req, loc, vhost)
   │    └─ "DELETE" → deleteH_.handle(req, loc, vhost)
   └─ attachSessionCookie(req, response)     ← bônus: gerencia cookie de sessão

6. GetHandler::handle()
   ├─ fsPath = loc.root + req.path()
   ├─ se é arquivo → serveFile(fsPath, vhost)
   │    └─ ResponseFactory::makeFile(fsPath, mime::fromPath(fsPath))
   ├─ se é diretório:
   │    ├─ tenta index (loc.index) → serveFile(...)
   │    └─ se loc.autoindex → ResponseFactory::makeAutoindex(fsPath, req.path())
   └─ senão → makeError(404, vhost)

7. Client::onWritable()            ← socket pronto para enviar
   ├─ outBuffer_ = response_.toString()   ← serializa HTTP
   ├─ send(fd_, outBuffer_.c_str() + outOffset_, ...)
   ├─ outOffset_ += bytes_enviados
   ├─ se outOffset_ >= outBuffer_.size():
   │    ├─ se !request_.keepAlive() → wantsClose_ = true, state_ = DONE
   │    └─ senão → state_ = READING_HEADERS, parser_.reset()
   └─ (send parcial é normal — próximo POLLOUT continua de onde parou)

8. EventLoop::reapClosed()
   └─ deleta Client com wantsClose()==true, fecha FD (RAII cuida disso)
```

---

## 5. Módulo `common/` — Fundação

Utilitários usados por todos os outros módulos. Não depende de nenhum outro módulo interno.

**Dono:** Membro 2. Todo o time usa.

---

### `Types.hpp` / `Types.cpp`

**Propósito:** define os tipos fundamentais compartilhados.

#### `struct CaseInsensitiveLess`

**O que faz:** um comparador para `std::map` que ignora maiúsculas/minúsculas ao comparar chaves de string.

**Por que existe:** headers HTTP são case-insensitive pela RFC 7230. `Content-Length` e `content-length` são o mesmo header. Se usarmos `std::map<std::string, std::string>` padrão, a comparação seria case-sensitive e `header("content-length")` não encontraria `"Content-Length"`. Com `CaseInsensitiveLess` como terceiro parâmetro do mapa, a busca funciona corretamente para qualquer capitalização.

```cpp
bool operator()(const std::string& a, const std::string& b) const;
// Implementação: compara char a char convertendo para minúsculo.
// Retorna true se 'a' vem antes de 'b' (ordem lexicográfica case-insensitive).
```

#### `typedef HeaderMap`

```cpp
typedef std::map<std::string, std::string, CaseInsensitiveLess> HeaderMap;
```

**Por que existe:** é o tipo que `Request` e `Response` usam para armazenar headers. Definir aqui como typedef garante que todos os módulos usem exatamente o mesmo tipo — se amanhã quisermos mudar a implementação interna, mudamos em um só lugar.

#### `typedef StringVec`

```cpp
typedef std::vector<std::string> StringVec;
```

**Por que existe:** conveniência. Usado em `ServerConfig::serverNames` (lista de nomes do servidor), `LocationConfig::methods` (GET, POST, DELETE) e em resultados de `str::split`.

---

### `HttpStatus.hpp` / `HttpStatus.cpp`

**Propósito:** catálogo de status codes HTTP e sua tradução para texto.

#### `enum HttpStatus`

Lista os status codes que o projeto precisa suportar:
- `200 OK` — requisição bem-sucedida
- `201 Created` — upload bem-sucedido
- `204 No Content` — DELETE bem-sucedido sem corpo
- `301 Moved Permanently` — redirect permanente
- `302 Found` — redirect temporário
- `400 Bad Request` — requisição malformada
- `403 Forbidden` — sem permissão de acesso
- `404 Not Found` — recurso não existe
- `405 Method Not Allowed` — método não permitido naquele location
- `408 Request Timeout` — cliente demorou demais para enviar
- `411 Length Required` — POST sem `Content-Length`
- `413 Payload Too Large` — body maior que `clientMaxBodySize`
- `414 URI Too Long` — URI excessivamente longa
- `500 Internal Server Error` — bug no servidor
- `501 Not Implemented` — método não suportado (ex: PUT)
- `505 HTTP Version Not Supported` — cliente mandou HTTP/2 ou HTTP/0.9

**Por que enum e não constantes soltas:** o compilador impede usar um número inválido acidentalmente. Além disso, é mais legível em `switch` e nos construtores de `Response`.

#### `const char* statusReason(int code)`

**O que faz:** converte um código inteiro na frase correspondente da RFC.
- Ex: `statusReason(404)` → `"Not Found"`.

**Por que existe:** a linha de status HTTP exige: `HTTP/1.1 404 Not Found\r\n`. O `Response::toString()` chama esta função para montar essa linha sem duplicar strings por todo o código.

---

### `FileDescriptor.hpp` / `FileDescriptor.cpp`

**Propósito:** wrapper RAII para file descriptors Unix.

**Contexto:** em Unix, tudo é um arquivo — sockets, pipes, arquivos normais. Todos são representados por um inteiro (`int fd`). Se você esquecer de chamar `close(fd)`, o sistema operacional mantém o recurso alocado para sempre (durante a vida do processo). Com dezenas de conexões simultâneas, isso esgota os FDs disponíveis e o servidor para de funcionar. O `valgrind --track-fds=yes` mostra isso e o avaliador da 42 vai checar.

#### `explicit FileDescriptor(int fd = -1)`

**O que faz:** constrói o wrapper armazenando o FD. O valor `-1` representa "sem FD válido" (convenção Unix).

**Por que `explicit`:** evita conversão implícita de inteiros comuns para `FileDescriptor`. Sem `explicit`, `FileDescriptor fd = 42` compilaria e seria ambíguo.

#### `~FileDescriptor()`

**O que faz:** se `fd_` for >= 0, chama `::close(fd_)`.

**Por que é crítico:** este é o coração do RAII. Quando um `Client` é deletado (seja por `reapClosed()` ou pelo destrutor do servidor), o destrutor do `FileDescriptor` fecha o socket automaticamente, sem precisar de `close()` espalhado pelo código.

#### `int get() const`

**O que faz:** retorna o FD sem transferir ownership.

**Quando usar:** sempre que precisar passar o FD para uma syscall (`send`, `recv`, `poll`), mas sem abandonar a propriedade.

#### `int release()`

**O que faz:** retorna o FD e seta `fd_` para `-1`, impedindo que o destrutor feche.

**Quando usar:** quando você quer transferir a ownership do FD para outro objeto. Exemplo: `Socket::accept()` cria um FD novo e retorna para o `Client` — o `Client` recebe via `release()` para que o `Socket` não feche o FD que o `Client` precisa.

#### `void reset(int fd = -1)`

**O que faz:** fecha o FD atual (se válido) e assume o novo FD passado.

**Quando usar:** quando um objeto precisa trocar de FD durante sua vida útil (ex: reconexão).

#### `bool valid() const`

**O que faz:** retorna `true` se `fd_ >= 0`.

**Quando usar:** verificações de sanidade antes de operações I/O.

#### Cópia proibida (private)

```cpp
FileDescriptor(const FileDescriptor&);
FileDescriptor& operator=(const FileDescriptor&);
```

**Por que:** se permitíssemos cópia, dois objetos teriam o mesmo FD e ambos tentariam chamar `close()` — double-close é undefined behavior. Proibindo cópia, o compilador gera erro se alguém tentar copiar por engano.

---

### `Socket.hpp` / `Socket.cpp`

**Propósito:** abstração de alto nível para sockets de servidor (listening socket).

Compõe um `FileDescriptor` internamente, herdando o comportamento RAII.

#### `void bindAndListen(const std::string& host, int port, int backlog = 128)`

**O que faz (quando implementado pelo Membro 1):**
1. `socket(AF_INET, SOCK_STREAM, 0)` — cria o FD
2. `setsockopt(SO_REUSEADDR)` — permite reiniciar o servidor sem esperar o timeout TCP
3. `bind(fd, addr, sizeof(addr))` — associa ao (host, port)
4. `listen(fd, backlog)` — coloca em modo de escuta com fila de 128 conexões pendentes

**Por que `backlog = 128`:** é o número de conexões que podem estar na fila aguardando `accept()` antes de o kernel começar a recusar novas. 128 é o valor padrão razoável para testes; `siege` pode estressar isso.

#### `int accept(struct sockaddr_in& outAddr)`

**O que faz:** chama `::accept()` no socket em escuta e preenche `outAddr` com as informações do cliente (IP, porta).

**Retorno:** o FD da nova conexão com o cliente, ou `-1` em erro.

**Por que não retorna `FileDescriptor`:** o chamador (`ListeningSocket::onReadable()`) precisa passar esse FD para o construtor de `Client`. Retornar um int evita problemas de transferência de ownership (em C++98 sem move semantics).

#### `void setNonBlocking(int fd)`

**O que faz:** `fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)`

**Por que é obrigatório:** sem esta chamada, `recv()` e `send()` bloqueiam o processo inteiro enquanto esperam dados. Com o FD em modo não-bloqueante, eles retornam imediatamente com `EAGAIN`/`EWOULDBLOCK` se não houver dados — e o `EventLoop` trata isso tentando novamente no próximo `poll()`.

---

### `Logger.hpp` / `Logger.cpp`

**Propósito:** log centralizado com filtro por nível de severidade.

#### `static Logger& instance()`

**O que faz:** implementa o Singleton com variável estática local (thread-safe em C++11, aceitável em C++98 single-thread):

```cpp
static Logger inst;
return inst;
```

**Por que static local:** garante inicialização na primeira chamada e destruição automática no fim do programa.

#### `enum Level { DEBUG, INFO, WARN, ERROR }`

- **DEBUG:** detalhes de baixo nível, desativado em produção (ex: "recebido POLLIN no fd 7")
- **INFO:** eventos normais (ex: "servidor iniciado na porta 8080")
- **WARN:** situações anormais mas recuperáveis (ex: "cliente desconectou abruptamente")
- **ERROR:** falhas graves (ex: "falha ao fazer bind() na porta 80")

#### `void setLevel(Level level)`

**O que faz:** define o nível mínimo. Mensagens abaixo desse nível são ignoradas.

**Caso de uso:** em testes de stress, setar `INFO` para não poluir o terminal. Durante debug, setar `DEBUG`.

#### `void log(Level level, const std::string& msg)`

**O que faz:** imprime `[NÍVEL] mensagem` em `stdout` (INFO/DEBUG) ou `stderr` (WARN/ERROR).

#### Macros `LOG_INFO`, `LOG_DEBUG`, `LOG_WARN`, `LOG_ERROR`

**Por que macros e não funções:** em C++98, não temos constexpr nem `__VA_ARGS__` portável. As macros encurtam a sintaxe de `Logger::instance().log(...)` para `LOG_INFO("msg")`, tornando o código mais legível.

---

### `StringUtils.hpp` / `StringUtils.cpp`

**Propósito:** funções utilitárias de string que o `ConfigParser` e o `RequestParser` precisam constantemente.

Todas estão no namespace `ws::str` para evitar colisão com funções do sistema.

#### `std::string trim(const std::string& s)`

**O que faz:** remove espaços/tabs/newlines do início e do fim.

**Por que existe:** o arquivo `.conf` tem linhas como `    listen      8080;   `. O parser precisa limpar esses espaços antes de converter para inteiro.

#### `std::string toLower(const std::string& s)` / `toUpper()`

**O que faz:** cria uma cópia da string toda em minúsculo/maiúsculo.

**Por que existe:** comparações case-insensitive (método HTTP, nome de header). Ex: `if (str::toLower(method) == "get")`.

#### `StringVec split(const std::string& s, char delim)`

**O que faz:** divide uma string pelo delimitador e retorna um vetor de partes.

**Por que existe:** `"GET, POST, DELETE"` → `{"GET", "POST", "DELETE"}`. Usado no parser de config para a diretiva `methods`.

#### `StringVec splitAny(const std::string& s, const std::string& delims)`

**O que faz:** como `split`, mas divide por qualquer caractere do conjunto `delims`, pulando sequências de delimitadores.

**Por que existe:** tokenizar linhas de config onde o separador pode ser espaço, tab ou ambos.

#### `bool startsWith(const std::string& s, const std::string& prefix)` / `endsWith()`

**Por que existem:** verificações comuns no parser: `startsWith(line, "server")`, `endsWith(path, ".py")`.

#### `bool iequals(const std::string& a, const std::string& b)`

**O que faz:** comparação case-insensitive sem alocar strings intermediárias (mais eficiente que `toLower(a) == toLower(b)`).

**Por que existe:** `Request::keepAlive()` precisa comparar `Connection: keep-alive` com `Connection: Keep-Alive`.

#### `std::string toString(long n)`

**O que faz:** converte inteiro para string via `ostringstream`.

**Por que não usar `std::to_string`:** não existe em C++98.

#### `long toLong(const std::string& s, bool& ok)`

**O que faz:** converte string para inteiro, indicando sucesso/falha via `ok` em vez de exceção.

**Por que `bool& ok` em vez de exceção:** o subject da 42 não exige tratamento de exceções e o compilador com `-Werror` pode reclamar de certos padrões. Além disso, em código de servidor, `try/catch` em cada conversão de número é verboso. O padrão `bool& ok` é idiomático em C++98.

---

## 6. Módulo `config/` — Configuração

**Dono:** Membro 2.

---

### `LocationConfig.hpp` / `LocationConfig.cpp`

**Propósito:** DTO que representa um bloco `location {}` do arquivo `.conf`.

#### Campos

| Campo | Tipo | Padrão | Significado |
|-------|------|--------|-------------|
| `path` | `string` | — | Prefixo de URI que ativa este location. Ex: `"/upload"` |
| `methods` | `StringVec` | — | Métodos permitidos. Ex: `["GET", "POST"]` |
| `root` | `string` | — | Diretório raiz no filesystem. Ex: `"./www"` |
| `index` | `string` | — | Arquivo index padrão. Ex: `"index.html"` |
| `autoindex` | `bool` | `false` | Se `true`, gera listagem de diretório quando não há index |
| `redirect` | `string` | — | Se não vazio, redireciona para esta URL |
| `redirectCode` | `int` | `302` | Código HTTP do redirect (301 ou 302) |
| `uploadStore` | `string` | — | Diretório onde arquivos POST são salvos |
| `cgi` | `map<ext, interpreter>` | — | Ex: `{".py": "/usr/bin/python3"}` |

#### `LocationConfig()` (construtor)

**O que faz:** inicializa os campos com valores padrão seguros (`autoindex = false`, `redirectCode = 302`, etc.). Sem isso, campos não inicializados em C++98 contêm lixo de memória.

---

### `ServerConfig.hpp` / `ServerConfig.cpp`

**Propósito:** DTO que representa um bloco `server {}` completo do arquivo `.conf`. Contém um ou mais `LocationConfig`.

#### Campos

| Campo | Tipo | Padrão | Significado |
|-------|------|--------|-------------|
| `host` | `string` | `"0.0.0.0"` | Interface de escuta |
| `port` | `int` | `80` | Porta de escuta |
| `serverNames` | `StringVec` | — | Nomes para virtual hosting. Ex: `["site-a.local"]` |
| `clientMaxBodySize` | `size_t` | `1MB` | Limite do body de POST. Acima disso → 413 |
| `errorPages` | `map<int, string>` | — | Mapa de código → caminho de arquivo. Ex: `{404: "/errors/404.html"}` |
| `locations` | `vector<LocationConfig>` | — | Lista de locations, em ordem de declaração |

#### `const LocationConfig* findLocation(const std::string& uriPath) const`

**O que faz (quando implementado pelo Membro 3):** percorre `locations` e retorna o ponteiro para o `LocationConfig` cujo `path` é o **maior prefixo** de `uriPath`.

**Por que longest-prefix match:** é o comportamento do Nginx. `/upload/img` combina com `/upload` antes de `/` porque `/upload` é mais específico. Se dois locations tivessem o mesmo comprimento de prefixo, o primeiro declarado no `.conf` vence.

**Por que retorna ponteiro e não referência:** `nullptr` é o sinal de "nenhum location combinou" → o router retorna 404.

---

### `ConfigParser.hpp` / `ConfigParser.cpp`

**Propósito:** lê e valida o arquivo `.conf` no estilo Nginx, produzindo um `vector<ServerConfig>`.

**Dono:** Membro 2.

#### `class ParseError : public std::runtime_error`

**O que faz:** exceção específica para erros de parsing, contendo o número da linha.

**Por que linha:** quando o `.conf` tem um erro de sintaxe, a mensagem `"ParseError: diretiva desconhecida na linha 42"` é muito mais útil do que `"ParseError: diretiva desconhecida"` num arquivo de 200 linhas.

#### `ParseError(const std::string& msg, std::size_t line)` / `std::size_t line() const`

**O que fazem:** constroem a exceção com mensagem e linha; `line()` permite ao chamador (o `main`) exibir a linha no log de erro.

#### `std::vector<ServerConfig> parseFile(const std::string& path)`

**O que faz (quando implementado pelo Membro 2):** abre o arquivo em `path`, lê todo o conteúdo para `source_` e delega para `parseString()`.

**Por que separar `parseFile` e `parseString`:** `parseString` pode ser chamada diretamente nos testes unitários com strings embutidas, sem precisar criar um arquivo em disco. Isso acelera muito o desenvolvimento e o debug do parser.

#### `std::vector<ServerConfig> parseString(const std::string& source)`

**O que faz:** recebe o texto bruto do `.conf`, inicia `pos_=0` e `line_=1`, e chama `doParse()`.

#### `(private) doParse()`, `parseServerBlock()`, `parseLocationBlock()`

**O que fazem:** implementam a state machine do parser:
- `doParse()`: loop principal; enquanto há tokens, espera encontrar a palavra `server`.
- `parseServerBlock()`: lê as diretivas dentro de `server { ... }` e, quando encontra `location`, chama `parseLocationBlock()`.
- `parseLocationBlock()`: lê as diretivas dentro de `location /path { ... }`.

**Por que state machine explícita:** o arquivo `.conf` tem blocos aninhados (`server > location`). Uma state machine com enum `{TOPLEVEL, IN_SERVER, IN_LOCATION}` torna as transições explícitas e os erros claros ("encontrei `location` fora de um bloco `server`").

#### `(private) nextToken()`, `expect(token)`, `skipWhitespace()`

**`nextToken()`:** avança `pos_` pulando whitespace e retorna a próxima palavra ou símbolo (`{`, `}`, `;`). Incrementa `line_` ao encontrar `\n`.

**`expect(token)`:** chama `nextToken()` e lança `ParseError` se o resultado não for o `token` esperado. Usado para validar a sintaxe: `expect("{")` depois de `server`.

**`skipWhitespace()`:** avança `pos_` sobre espaços, tabs e newlines, atualizando `line_`.

---

## 7. Módulo `core/` — Motor de Rede

**Dono:** Membro 1.

---

### `IPollable.hpp`

**Propósito:** interface que qualquer objeto monitorável pelo `EventLoop` deve implementar.

Este é o **contrato central** do Reactor Pattern.

#### `virtual int fd() const = 0`

**O que faz:** retorna o file descriptor que o `EventLoop` deve passar para `poll()`.

**Por que virtual puro:** obriga cada implementador a definir explicitamente qual FD monitorar. Sem isso, o loop não saberia qual FD associar ao objeto.

#### `virtual short interest() const = 0`

**O que faz:** retorna os eventos que o objeto quer monitorar neste momento: `POLLIN` (quer ler), `POLLOUT` (quer escrever) ou a combinação `POLLIN | POLLOUT`.

**Por que dinâmico:** um `Client` no estado `READING_HEADERS` retorna `POLLIN`. No estado `WRITING_RESPONSE` retorna `POLLOUT`. O `EventLoop` chama `interest()` a cada iteração para saber o que monitorar — isso é mais eficiente do que sempre monitorar os dois eventos.

#### `virtual void onReadable() = 0` / `virtual void onWritable() = 0` / `virtual void onHangup() = 0`

**O que fazem:** callbacks chamados pelo `EventLoop` quando o evento correspondente ocorrer no FD do objeto.

**Por que não uma função genérica `onEvent(int events)`:** callbacks separados tornam o código de cada implementador mais limpo e auto-documentado. `ListeningSocket::onReadable()` chama `accept()` — não faz sentido ter `onWritable()` nele.

**`onHangup()`:** chamado quando o cliente fecha a conexão (`POLLHUP`). O objeto deve marcar `wantsClose() = true` e o loop o remove na próxima limpeza.

#### `virtual bool wantsClose() const = 0`

**O que faz:** sinaliza ao `EventLoop` que o objeto quer ser removido e deletado.

**Por que não deletar imediatamente no `onHangup()`:** o loop está iterando sobre a lista de `pollables_` quando o `onHangup()` é chamado. Deletar dentro da iteração é undefined behavior. O `wantsClose()` adia a remoção para o `reapClosed()`, que ocorre depois da iteração.

---

### `EventLoop.hpp` / `EventLoop.cpp`

**Propósito:** implementação do Reactor — o único ponto onde `poll()` é chamado.

#### `void add(IPollable* pollable)`

**O que faz:** adiciona o ponteiro à lista `pollables_`.

**Quando chamado:** quando `ListeningSocket::onReadable()` cria um novo `Client`, ele chama `loop_.add(client)` para que o loop passe a monitorar o socket do cliente.

#### `void remove(IPollable* pollable)`

**O que faz:** encontra e remove o ponteiro da lista (sem deletar o objeto).

**Quando chamado:** antes de deletar um objeto manualmente (quando há ownership explícito).

#### `void runOnce(int timeoutMs)`

**O que faz (quando implementado pelo Membro 1):**
1. Constrói um array `struct pollfd[]` com um elemento por `IPollable*`, preenchendo `.fd = p->fd()` e `.events = p->interest()`.
2. Chama `poll(fds, count, timeoutMs)`.
3. Itera o resultado: para cada FD com `revents & POLLIN` → `p->onReadable()`; `revents & POLLOUT` → `p->onWritable()`; `revents & (POLLHUP | POLLERR)` → `p->onHangup()`.
4. Também chama `waitpid(-1, &status, WNOHANG)` para colher processos CGI terminados.

**`timeoutMs = 1000`:** o loop não fica preso para sempre se não houver eventos. A cada segundo ele acorda, pode fazer GC de sessões expiradas, verificar timeouts de cliente, etc.

#### `void run()`

**O que faz:** seta `running_ = true` e chama `runOnce(1000)` em loop até `running_` ser `false`.

#### `void stop()`

**O que faz:** seta `running_ = false`, fazendo o `run()` sair após a iteração atual.

**Quando chamado:** no handler de `SIGINT`/`SIGTERM` para shutdown gracioso.

#### `bool isRunning() const`

**Por que existe:** permite que outros objetos consultem se o loop ainda está rodando (útil para shutdown coordenado).

#### `(private) reapClosed()`

**O que faz:** percorre `pollables_`, remove os que têm `wantsClose() == true`, deleta cada um e registra no log.

**Por que separado de `runOnce()`:** garante que a deleção ocorre fora da iteração principal sobre `pollables_`, evitando iterator invalidation.

---

### `Server.hpp` / `Server.cpp`

**Propósito:** ponto de entrada do servidor. Inicializa sockets e dá início ao loop.

Contém duas classes no mesmo arquivo: `ListeningSocket` e `Server`.

---

#### `class ListeningSocket : public IPollable`

Representa um socket de escuta (`bind + listen`) e sabe criar `Client`s ao receber conexões.

##### `ListeningSocket(host, port, vhosts, router, sessions, loop)`

**O que faz (quando implementado pelo Membro 1):**
1. Chama `socket_.bindAndListen(host, port)`.
2. Chama `socket_.setNonBlocking(socket_.fd())`.
3. Armazena referências ao `router_`, `sessions_`, `loop_` — vai precisar delas em `onReadable()`.

**Por que recebe o `loop_` por referência:** quando `onReadable()` cria um novo `Client`, ele precisa chamá-lo `loop_.add(client)`. Injetar o loop no construtor é a forma de dar acesso sem tornar o loop global.

##### `int fd() const`

Retorna `socket_.fd()` para que o `EventLoop` monitore este socket com `poll()`.

##### `short interest() const`

Retorna sempre `POLLIN` — um listening socket só precisa de `onReadable()` para fazer `accept()`.

##### `void onReadable()`

**O que faz (quando implementado pelo Membro 1):**
1. Chama `socket_.accept(addr)` em loop até retornar -1 (pode haver múltiplas conexões na fila).
2. Para cada FD aceito: `socket_.setNonBlocking(client_fd)`.
3. `new Client(client_fd, vhosts_, router_, sessions_)`.
4. `loop_.add(client)`.

**Por que loop em `accept()`:** em condições de alto tráfego, várias conexões podem acumular na fila. Aceitar todas de uma vez evita que o próximo `poll()` sature com POLLIN repetido no mesmo FD.

##### `void onWritable()` / `void onHangup()`

Um listening socket nunca escreve. `onWritable()` é no-op. `onHangup()` não deve ocorrer — se ocorrer, indica erro grave e deve ser logado.

##### `bool wantsClose() const`

Retorna `false` — um listening socket só fecha quando o servidor faz `stop()`.

---

#### `class Server`

##### `Server(configs, router)`

**O que faz:** armazena as configs, o router (por referência) e inicializa o `EventLoop` e `SessionStore` internos.

**Por que `Router&` e não `Router` por valor:** o `Router` é pesado (tem os 3 handlers dentro) e é compartilhado por todos os `Client`s. Passar por valor criaria uma cópia desnecessária.

##### `void start()`

**O que faz (quando implementado pelo Membro 1):**
1. Chama `groupConfigsByEndpoint()` para identificar pares (host, port) únicos.
2. Para cada par único, cria um `ListeningSocket` com a lista de `ServerConfig` que compartilham aquele endpoint (virtual hosting).
3. Registra cada `ListeningSocket` no `loop_`.
4. Chama `loop_.run()`.

**Por que agrupar por endpoint:** duas configs `server_name site-a.local` e `server_name site-b.local` podem escutar na mesma porta 8080. Elas compartilham um único `ListeningSocket` — o `Client` decide qual vhost usar pelo header `Host`.

##### `void stop()`

Chama `loop_.stop()`.

##### `EventLoop& loop()` / `SessionStore& sessions()`

Accessors que expõem componentes internos para o `main()` (e para testes).

---

### `Client.hpp` / `Client.cpp`

**Propósito:** representa uma conexão ativa com um cliente HTTP. Implementa a state machine de leitura → parsing → roteamento → escrita.

#### `enum State`

| Estado | Significado |
|--------|-------------|
| `READING_HEADERS` | Recebendo a request line e os headers |
| `READING_BODY` | Recebendo o body (POST) |
| `ROUTING` | Parser completou; chamando `Router::route()` |
| `WRITING_RESPONSE` | Enviando a resposta serializada pelo socket |
| `DONE` | Resposta completa enviada; socket pode ser fechado |

#### `Client(fd, vhosts, router, sessions)`

**O que faz:**
- Armazena o FD recebido em `fd_` (RAII cuida do close).
- Inicia `state_ = READING_HEADERS`.
- Armazena referências para os recursos compartilhados: `vhosts_` (para virtual hosting), `router_` (para roteamento), `sessions_` (para cookies de sessão).
- Registra `lastActivity_ = time(0)` para detecção de timeout.

#### `int fd() const`

Retorna o FD do socket do cliente, para o `EventLoop`.

#### `short interest() const`

Retorna `POLLIN` quando `state_` é `READING_*`, e `POLLOUT` quando é `WRITING_RESPONSE`. Retorna 0 em `DONE` — o loop vai removê-lo logo via `wantsClose()`.

#### `void onReadable()`

**O que faz (quando implementado pelo Membro 1):**
1. `recv(fd_.get(), buf, sizeof(buf), 0)` — leitura não-bloqueante.
2. Se retornou 0 → cliente fechou a conexão → `wantsClose_ = true`.
3. Se retornou -1 com `EAGAIN/EWOULDBLOCK` → sem dados agora → aguarda próximo POLLIN.
4. Se retornou > 0 → `parser_.feed(buf, n, clientMaxBodySize)`.
5. Se resultado for `COMPLETE` → `state_ = ROUTING` → chama `router_.route(request_, matchVirtualHost())` → armazena resultado em `response_` → `outBuffer_ = response_.toString()` → `state_ = WRITING_RESPONSE`.
6. Se resultado for `BAD_REQUEST`, `BODY_TOO_LARGE`, etc. → `buildErrorResponse(400)` → `state_ = WRITING_RESPONSE`.

#### `void onWritable()`

**O que faz (quando implementado pelo Membro 1):**
1. `send(fd_.get(), outBuffer_.c_str() + outOffset_, remaining, 0)`.
2. `outOffset_ += bytes_enviados`.
3. Se `outOffset_ >= outBuffer_.size()` → envio completo.
   - Se `!request_.keepAlive()` → `wantsClose_ = true`, `state_ = DONE`.
   - Senão → `parser_.reset()`, `state_ = READING_HEADERS` (reutiliza conexão).
4. Se `send()` retornou -1 com `EAGAIN` → não fez nada; aguarda próximo POLLOUT.

**Por que offset:** `send()` pode enviar menos bytes do que pedido (envio parcial). O offset garante que o próximo `onWritable()` continue de onde parou, sem reenviar o que já foi.

#### `void onHangup()`

Seta `wantsClose_ = true`. O cliente fechou a conexão abruptamente.

#### `bool wantsClose() const`

Retorna `wantsClose_`. Quando `true`, o `reapClosed()` vai deletar este `Client`, fechando o FD pelo RAII.

#### `std::time_t lastActivity() const`

Retorna o timestamp da última atividade. Útil para o servidor implementar timeout: se `time(0) - client.lastActivity() > 60`, fechar a conexão com 408.

#### `(private) matchVirtualHost() const`

**O que faz:** busca em `vhosts_` o `ServerConfig` cujo `serverNames` inclui o valor do header `Host` da requisição. Se não encontrar nenhum, retorna o primeiro (comportamento padrão do Nginx).

**Por que aqui e não no Router:** o virtual hosting é uma decisão de rede (depende do header `Host` e da porta de chegada), não de HTTP lógico. Faz sentido no `Client`.

#### `(private) buildErrorResponse(int code)`

Atalho para `response_ = ResponseFactory::makeError(code, matchVirtualHost())`. Centraliza a montagem de resposta de erro no `Client`.

---

## 8. Módulo `http/` — Lógica HTTP

---

### `Request.hpp` / `Request.cpp`

**Propósito:** DTO imutável representando uma requisição HTTP já parseada.

**Dono:** Membro 2 (escreve o `RequestParser` que preenche); Membro 3 (lê os dados); Membro 1 (passa o buffer bruto para o parser).

#### Campos privados

| Campo | Conteúdo (exemplo) |
|-------|--------------------|
| `method_` | `"GET"`, `"POST"`, `"DELETE"` |
| `uri_` | `"/upload/foto.png?thumb=1"` |
| `path_` | `"/upload/foto.png"` (sem query) |
| `query_` | `"thumb=1"` (sem `?`) |
| `version_` | `"HTTP/1.1"` |
| `body_` | Conteúdo bruto do corpo da requisição |
| `headers_` | `HeaderMap` com todos os headers |

**Por que `path_` e `query_` separados de `uri_`:** o CGI precisa de `PATH_INFO` e `QUERY_STRING` como variáveis de ambiente separadas. O `Router` faz longest-prefix match no `path_`, não no `uri_` completo. Separar desde o parser evita que cada módulo reimplemente a divisão.

**Por que campos privados:** o `Request` é um DTO imutável após construção. Somente o `RequestParser` (via `friend class`) pode escrever nos campos. Isso garante que o Membro 3 não acidentalmente altera dados do Membro 2.

#### `const std::string& method() const` ... `const std::string& body() const`

Getters de acesso de leitura. Retornam por `const ref` para evitar cópia desnecessária.

#### `std::string header(const std::string& name) const`

**O que faz:** busca no `HeaderMap` pelo `name`. Como o mapa usa `CaseInsensitiveLess`, `header("Content-Length")` e `header("content-length")` retornam o mesmo valor.

**Retorna string por valor** (não por referência) para poder retornar `""` quando o header não existe, sem dangling reference.

#### `bool hasHeader(const std::string& name) const`

**Por que existe:** há diferença entre `header("X-Missing")` retornando `""` e o header realmente não existir. Por exemplo, `Content-Length: ` com valor vazio é diferente de ausência do header.

#### `std::string cookie(const std::string& name) const`

**O que faz (quando implementado pelo Membro 3):** parseia o header `Cookie: name1=val1; name2=val2` e retorna o valor da chave `name`.

**Por que no `Request` e não no `Router`:** é uma operação de leitura da requisição. Manter próximo dos dados é mais coeso.

#### `bool keepAlive() const`

**O que faz:** retorna `true` se a conexão deve ser mantida após a resposta.
- Em HTTP/1.1: padrão é keep-alive; `Connection: close` desativa.
- Em HTTP/1.0: padrão é fechar; `Connection: keep-alive` ativa.

**Por que no `Request`:** o `Client::onWritable()` usa isso para decidir se fecha o socket ou reutiliza.

#### `friend class RequestParser`

**Por que:** `RequestParser` é o único que pode escrever em `Request::method_`, `uri_`, etc. `friend` é a forma em C++ de dar acesso privilegiado a uma classe específica sem tornar os campos públicos.

---

### `RequestParser.hpp` / `RequestParser.cpp`

**Propósito:** converte bytes brutos do socket em um objeto `Request`.

**Dono:** Membro 2.

#### `enum State`

| Estado | Significado |
|--------|-------------|
| `METHOD` | Lendo o método HTTP (`GET`, `POST`, `DELETE`) |
| `URI` | Lendo a URI (`/path?query`) |
| `VERSION` | Lendo a versão (`HTTP/1.1`) |
| `HEADER` | Lendo headers linha a linha |
| `BODY_LENGTH` | Lendo body com `Content-Length` bytes fixos |
| `BODY_CHUNKED` | Lendo body em formato chunked |
| `DONE` | Parsing completo |
| `ERROR` | Parsing falhou (400, 413, etc.) |

#### `enum FeedResult`

| Resultado | Significado |
|-----------|-------------|
| `NEED_MORE` | Dados insuficientes; esperar mais bytes |
| `COMPLETE` | Parsing concluído com sucesso |
| `BAD_REQUEST` | Malformação → 400 |
| `URI_TOO_LONG` | URI excede limite → 414 |
| `BODY_TOO_LARGE` | Body > `clientMaxBodySize` → 413 |
| `HTTP_VERSION_UNSUPPORTED` | Versão não suportada → 505 |

#### `void reset()`

**O que faz:** reinicia todos os estados para parsing de uma nova requisição na mesma conexão (keep-alive).

#### `FeedResult feed(const char* data, std::size_t n, std::size_t maxBody)`

**O que faz (quando implementado pelo Membro 2):**
1. Appenda `data[0..n]` no buffer interno `buf_`.
2. Dependendo de `state_`, chama o método correspondente.
3. Retorna o resultado da tentativa de parsing.

**Por que recebe `maxBody`:** vem de `serverConfig.clientMaxBodySize`. O parser valida o limite durante o parsing, não depois — isso evita que o servidor aloque memória para um body gigante antes de rejeitar.

#### `Request take()`

**O que faz:** retorna o `Request` construído e reinicia o parser internamente.

**Semântica de "take":** o chamador assume ownership dos dados. Após `take()`, o `RequestParser` está pronto para parsear a próxima requisição na mesma conexão.

#### `int errorStatus() const`

Retorna o HTTP status code correspondente ao erro ocorrido (400, 413, 414, 505). Usado pelo `Client::onReadable()` para construir a resposta de erro correta.

#### `(private) parseRequestLine()`, `parseHeaders()`, `parseBodyByLength()`, `parseBodyChunked()`, `splitUri()`

Cada um processa uma fase do parsing:

**`parseRequestLine()`:** busca o padrão `METHOD URI VERSION\r\n` no buffer. Valida que o método é um token ASCII sem espaços, que a URI começa com `/`, e que a versão é `HTTP/1.1` ou `HTTP/1.0`.

**`parseHeaders()`:** lê linhas `Key: Value\r\n` até encontrar a linha vazia `\r\n`. Armazena em `building_.headers_`. Ao encontrar a linha vazia, analisa `Content-Length` e `Transfer-Encoding` para decidir o próximo estado.

**`parseBodyByLength(maxBody)`:** lê exatamente `contentLength_` bytes. Rejeita se `contentLength_ > maxBody` → `BODY_TOO_LARGE`.

**`parseBodyChunked(maxBody)`:** implementa o protocolo chunked: cada chunk começa com seu tamanho em hex seguido de `\r\n`, depois os dados, depois `\r\n`. Um chunk de tamanho 0 indica fim.

**`splitUri()`:** divide `building_.uri_` em `building_.path_` (antes do `?`) e `building_.query_` (depois do `?`).

---

### `Response.hpp` / `Response.cpp`

**Propósito:** DTO mutável que representa uma resposta HTTP a ser enviada.

**Dono:** Membro 3 (constrói); Membro 1 (serializa e envia).

#### `Response()` / `explicit Response(int status)`

**Por que dois construtores:** o padrão cria uma resposta 200 vazia (útil em construção incremental). O explícito permite criar já com status definido: `Response r(404)`.

**Por que `explicit` no segundo:** evita conversão implícita de `int` para `Response` — `return 404` não compilaria onde se espera uma `Response`.

#### `void setStatus(int code)`

Seta o código de status. Útil quando o status só é conhecido depois de verificar o filesystem.

#### `void setHeader(const std::string& key, const std::string& value)`

Adiciona ou sobrescreve um header na `HeaderMap`.

#### `void setBody(const std::string& body)`

**O que faz:** seta o body E automaticamente atualiza o header `Content-Length` para `body.size()`. Isso evita um erro clássico: setar body e esquecer de atualizar o `Content-Length`, causando que o cliente leia mais ou menos bytes do que enviados.

#### `void appendBody(const std::string& chunk)`

**O que faz:** concatena ao body existente e atualiza `Content-Length`. Usado quando o corpo é gerado em partes (ex: listagem de diretório line-by-line).

#### `void setCookie(const std::string& name, const std::string& value, const std::string& options)`

**O que faz (quando implementado pelo Membro 3):** adiciona um header `Set-Cookie: name=value; options`. Múltiplos cookies exigem múltiplos headers `Set-Cookie` — o `HeaderMap` normalmente sobrescreve valores duplicados, então a implementação precisa tratar isso (ex: acumular em uma chave especial ou usar `multimap`).

**`options` padrão = `"Path=/"`:** garante que o cookie seja enviado para todas as URLs do domínio.

#### `std::string toString() const`

**O que faz:** serializa o objeto na forma textual exigida pela RFC:
```
HTTP/1.1 200 OK\r\n
Content-Type: text/html\r\n
Content-Length: 42\r\n
\r\n
<body>
```

Este é o método que o `Client::onWritable()` chama para obter os bytes que serão enviados pelo socket.

---

### `ResponseFactory.hpp` / `ResponseFactory.cpp`

**Propósito:** funções de fábrica que centralizam a criação de respostas comuns.

**Dono:** Membro 3.

Implementado como namespace de funções livres em vez de classe com métodos estáticos — mais idiomático em C++ para utilitários sem estado.

#### `Response makeError(int code, const ServerConfig& cfg)`

**O que faz:** cria uma resposta de erro:
1. Verifica se `cfg.errorPages[code]` existe → lê o arquivo e usa como body.
2. Se não existir, usa uma página embutida genérica.
3. Seta `Content-Type: text/html` e `Content-Length`.

**Por que recebe `ServerConfig&`:** cada servidor virtual pode ter páginas de erro customizadas diferentes. A página do `site-a.local` pode ser diferente da do `site-b.local`.

#### `Response makeRedirect(const std::string& url, int code)`

**O que faz:** cria uma resposta com `Location: url` e body vazio. `code` padrão = 302 (temporário).

**Por que separar 301 e 302:** `redirect 301` diz ao navegador para cachear o redirect permanentemente. `redirect 302` é temporário. A `LocationConfig` tem `redirectCode` para configurar isso.

#### `Response makeFile(const std::string& fsPath, const std::string& mime)`

**O que faz (quando implementado pelo Membro 3):**
1. Abre o arquivo em `fsPath`.
2. Lê o conteúdo.
3. Seta `Content-Type: mime`, `Content-Length: tamanho`, body = conteúdo.

**Por que recebe o mime separado:** o chamador (`GetHandler`) já calculou o MIME type via `mime::fromPath()`. Separar evita que `makeFile` tenha que reimplementar a detecção.

#### `Response makeAutoindex(const std::string& fsPath, const std::string& uriPath)`

**O que faz (quando implementado pelo Membro 3):** gera um HTML com listagem de diretório:
```html
<html><body>
<h1>Index of /uploads/</h1>
<ul>
  <li><a href="foto.png">foto.png</a> (45 KB)</li>
  ...
</ul>
</body></html>
```

**Por que recebe `uriPath`:** os links na listagem devem ser URLs relativas (ex: `/uploads/foto.png`), não caminhos do filesystem.

#### `Response makeFromCgi(const std::string& rawCgiOutput)`

**O que faz (quando implementado pelo Membro 3):** parseia a saída bruta do script CGI. A saída CGI começa com seus próprios headers seguidos de `\r\n\r\n` e depois o body. Este método extrai os headers e o body e os move para um objeto `Response` com status correto.

---

### `MimeTypes.hpp` / `MimeTypes.cpp`

**Propósito:** mapeia extensões de arquivo para Content-Type.

#### `std::string fromExtension(const std::string& ext)`

**O que faz:** dada uma extensão (sem ponto, ex: `"html"`), retorna o MIME type.

Mapeamentos implementados:
- `html`, `htm` → `text/html`
- `css` → `text/css`
- `js` → `application/javascript`
- `json` → `application/json`
- `txt` → `text/plain`
- `png` → `image/png`
- `jpg`, `jpeg` → `image/jpeg`
- `gif` → `image/gif`
- `svg` → `image/svg+xml`
- `ico` → `image/x-icon`
- `pdf` → `application/pdf`
- Qualquer outra coisa → `application/octet-stream`

**Por que `application/octet-stream` como fallback:** informa ao navegador que ele não deve tentar renderizar o arquivo, mas fazer download. É o comportamento correto para extensões desconhecidas.

#### `std::string fromPath(const std::string& path)`

**O que faz:** extrai a extensão de um caminho completo e chama `fromExtension()`.

**Exemplo:** `fromPath("./www/img/logo.svg")` → `"image/svg+xml"`.

---

### `Router.hpp` / `Router.cpp`

**Propósito:** direciona uma `Request` ao handler correto baseado no método HTTP e nas regras de `LocationConfig`.

**Dono:** Membro 3.

#### `Router(configs, sessions)`

**O que faz:** armazena referências a `configs_` (para consultas de virtual hosting) e `sessions_` (para cookie de sessão). Cria instâncias dos handlers (`GetHandler`, `PostHandler`, `DeleteHandler`).

**Por que os handlers são membros e não locais:** evita criação/destruição a cada requisição. Em C++98 sem otimizações, isso importa.

#### `Response route(const Request& req, const ServerConfig& vhost)`

**O que faz (quando implementado pelo Membro 3):**
1. `loc = vhost.findLocation(req.path())` → longest-prefix match.
2. Se `loc == nullptr` → `makeError(404, vhost)`.
3. Se `!methodAllowed(req.method(), *loc)` → `makeError(405, vhost)`.
4. Se `loc.redirect` não vazio → `makeRedirect(loc.redirect, loc.redirectCode)`.
5. Despacha para o handler:
   - `"GET"` → `getH_.handle(req, *loc, vhost)`
   - `"POST"` → `postH_.handle(req, *loc, vhost)`
   - `"DELETE"` → `deleteH_.handle(req, *loc, vhost)`
   - Outro → `makeError(405, vhost)`
6. `attachSessionCookie(req, response)` → adiciona `Set-Cookie` se necessário.
7. Retorna a `Response`.

#### `(private) methodAllowed(method, loc)`

**O que faz:** verifica se `method` está em `loc.methods`. Se `loc.methods` estiver vazio, todos os métodos são permitidos.

#### `(private) attachSessionCookie(req, resp)`

**O que faz (quando implementado pelo Membro 3):**
1. Lê o cookie `WEBSERV_SESSION` da `req`.
2. Chama `sessions_.getOrCreate(sid)`.
3. Se o ID foi criado agora → `resp.setCookie("WEBSERV_SESSION", newId)`.
4. Se o ID já existia → `sessions_` já atualizou o TTL em `getOrCreate`.

---

### `IMethodHandler.hpp` e handlers

**Propósito:** interface do padrão Strategy para handlers de métodos HTTP.

#### `class IMethodHandler`

Interface pura com um único método:

```cpp
virtual Response handle(const Request& req,
                        const LocationConfig& loc,
                        const ServerConfig& srv) = 0;
```

**Por que `LocationConfig` separado de `ServerConfig`:** o handler já recebe a location correta (pré-selecionada pelo Router). Ele não precisa reimplementar o longest-prefix match — só usa os dados da location.

---

#### `class GetHandler`

##### `Response handle(...)`

Ponto de entrada do Strategy para requisições GET. Delega para `serveFile()` ou `serveDirectory()`.

##### `(private) serveFile(fsPath, srv)`

**O que faz:** verifica permissão de leitura, chama `ResponseFactory::makeFile()`. Se o arquivo não existir → `makeError(404)`. Se não tiver permissão → `makeError(403)`.

##### `(private) serveDirectory(fsPath, uriPath, loc, srv)`

**O que faz:**
1. Se `loc.index` não vazio e `fsPath/index` existe → `serveFile(fsPath/loc.index, srv)`.
2. Se `loc.autoindex == true` → `makeAutoindex(fsPath, uriPath)`.
3. Senão → `makeError(403)` (acesso a diretório sem index é proibido por padrão).

---

#### `class PostHandler`

##### `Response handle(...)`

Decide se o POST é um upload de arquivo ou uma requisição CGI:
- Se `loc.cgi` tem uma entrada para a extensão da URI → `handleCgi()`.
- Senão → `handleUpload()`.

##### `(private) handleUpload(req, loc, srv)`

**O que faz:**
1. Valida que `loc.uploadStore` não está vazio.
2. Extrai o nome do arquivo do header `Content-Disposition` ou da URI.
3. Escreve `req.body()` em `loc.uploadStore/filename`.
4. Retorna `Response(201)` com `Content-Location` apontando para o arquivo.

##### `(private) handleCgi(req, loc, srv, interpreter)`

**O que faz:** cria um `CgiHandler`, chama `start()` para fazer fork/exec e — de forma síncrona no stub — retorna a resposta. A versão assíncrona (com o `CgiHandler` como `IPollable` no loop) é a integração a ser feita em conjunto na Fase 3.

---

#### `class DeleteHandler`

##### `Response handle(...)`

**O que faz:**
1. `fsPath = loc.root + req.path()`.
2. Verifica que o arquivo existe → se não → `makeError(404)`.
3. Verifica que é um arquivo (não diretório) → se diretório → `makeError(403)`.
4. `unlink(fsPath.c_str())` → se falhar → `makeError(500)`.
5. Retorna `Response(204)` (sucesso sem body).

---

## 9. Módulo `cgi/` — Common Gateway Interface

**Dono:** os 3 membros colaboram; execução do processo (fork/pipe) é do Membro 1.

---

### `CgiEnv.hpp` / `CgiEnv.cpp`

**Propósito:** constrói o array de variáveis de ambiente que o script CGI espera.

O padrão CGI (RFC 3875) define variáveis obrigatórias que o script lê via `getenv()`.

#### `CgiEnv(req, loc, srv, scriptPath)`

**O que faz (quando implementado pelo Membro 3):** chama `build()` que popula `entries_` com strings no formato `"KEY=VALUE"`:

| Variável | Valor | Fonte |
|----------|-------|-------|
| `REQUEST_METHOD` | `"POST"` | `req.method()` |
| `SCRIPT_NAME` | `"/cgi-bin/hello.py"` | `req.path()` |
| `PATH_INFO` | porção após o script | da URI |
| `QUERY_STRING` | `"thumb=1"` | `req.query()` |
| `SERVER_NAME` | `"localhost"` | `srv.serverNames[0]` |
| `SERVER_PORT` | `"8080"` | `srv.port` |
| `SERVER_PROTOCOL` | `"HTTP/1.1"` | constante |
| `GATEWAY_INTERFACE` | `"CGI/1.1"` | constante |
| `CONTENT_TYPE` | `"application/json"` | `req.header("Content-Type")` |
| `CONTENT_LENGTH` | `"42"` | `req.header("Content-Length")` |
| `HTTP_*` | headers como env vars | todos os `req.headers()` prefixados com `HTTP_` |

#### `char** asEnvp()`

**O que faz:** converte `entries_` (vector de strings) para `char**` (array de ponteiros), formato que `execve()` espera.

**Por que armazenar `envp_` como membro:** `char**` aponta para os `.c_str()` das strings em `entries_`. Se `entries_` for local e destruído, os ponteiros ficam dangling. Como membro, ambos vivem o mesmo tempo.

#### `const std::vector<std::string>& asVector() const`

**Por que existe:** permite testes unitários inspecionarem as variáveis geradas sem precisar chamar `execve()`.

---

### `CgiHandler.hpp` / `CgiHandler.cpp`

**Propósito:** executa um script externo de forma não-bloqueante, integrando-se ao `EventLoop` como mais um `IPollable`.

**Este é o componente mais complexo do projeto.**

#### `CgiHandler(req, loc, srv, interpreter, scriptPath)`

**O que faz:** armazena todas as informações necessárias para o `fork/exec` e inicializa o estado (`pid_ = -1`, `done_ = false`, etc.).

**Por que não fazer o fork aqui:** a execução (fork, pipe, execve) acontece em `start()`, que recebe o `loop`. Separar construção de execução facilita testes e evita que o construtor lance exceções.

#### `void start(EventLoop& loop)`

**O que faz (quando implementado pelo Membro 1):**
1. `pipe(stdin_pipe)` e `pipe(stdout_pipe)` — dois pares de FDs.
2. `fork()`:
   - **Filho:** `dup2(stdin_pipe[0], STDIN_FILENO)`, `dup2(stdout_pipe[1], STDOUT_FILENO)`, fechar FDs desnecessários, `CgiEnv env(...)`, `execve(interpreter, args, env.asEnvp())`.
   - **Pai:** fecha as pontas que o filho usa, armazena `stdinPipe_ = stdin_pipe[1]` (escreve o body) e `stdoutPipe_ = stdout_pipe[0]` (lê a resposta).
3. `setNonBlocking(stdinPipe_.get())` e `setNonBlocking(stdoutPipe_.get())`.
4. `loop.add(this)` — o loop começa a monitorar ambos os pipes.
5. `startedAt_ = time(0)`.

#### `int fd() const`

Retorna `stdoutPipe_.get()` — o `EventLoop` monitora este FD para saber quando o script produziu output.

**Problema:** o `CgiHandler` tem dois FDs (stdin e stdout). O `IPollable` expõe apenas um. A solução prática é registrar o `CgiHandler` duas vezes no loop (uma vez para cada pipe) ou criar um wrapper. Esta decisão fica para o Membro 1 na implementação.

#### `short interest() const`

- `POLLOUT` enquanto há body para escrever no stdin do script.
- `POLLIN` enquanto aguarda output do script.
- `POLLIN | POLLOUT` quando ambos acontecem.

#### `void onWritable()`

**O que faz:** `send(stdinPipe_.get(), req.body().c_str() + stdinOffset_, remaining)`. Atualiza `stdinOffset_`. Quando todo o body foi enviado, fecha `stdinPipe_` (o script detecta EOF no stdin).

#### `void onReadable()`

**O que faz:** `recv(stdoutPipe_.get(), buf, sizeof(buf))`. Acumula em `output_`. Quando `recv()` retorna 0 (EOF) → o script terminou → `done_ = true`.

#### `void onHangup()`

Chamado quando o pipe fecha abruptamente. Seta `wantsClose_ = true`.

#### `bool finished() const` / `Response takeResponse()`

`finished()` retorna `done_`. `takeResponse()` chama `ResponseFactory::makeFromCgi(output_)` e retorna a resposta. Chamado pelo `Client` quando o CGI termina.

#### `bool timedOut(int timeoutSec) const`

**O que faz:** `time(0) - startedAt_ > timeoutSec`. O `EventLoop::runOnce()` chama isso para cada `CgiHandler` ativo e, se `true`, chama `kill()` e depois gera uma resposta 500.

**Por que timeout:** scripts mal escritos ou em loop infinito travariam o slot de conexão indefinidamente. O subject exige que o servidor não trave.

#### `void kill()`

`kill(pid_, SIGKILL)` — mata o processo filho. Chamado após timeout.

---

## 10. Módulo `session/` — Sessões e Cookies

**Dono:** Membro 3. Integrado ao `SessionStore` do `Server` e ao `Router`.

---

### `Session.hpp` / `Session.cpp`

**Propósito:** representa uma sessão individual de um usuário, com dados chave-valor e tempo de expiração.

#### `Session()` / `Session(const std::string& id)`

O construtor padrão existe para que `Session` possa ser valor em `std::map` (exigido pelo compilador C++98 ao inserir em `map`). O segundo cria uma sessão com ID específico.

#### `const std::string& id() const`

Retorna o ID único da sessão — o valor que vai no cookie `WEBSERV_SESSION`.

#### `bool has(const std::string& key) const`

Verifica se uma chave existe na sessão. Útil para não chamar `set()` com sobrescrita acidental.

#### `std::string get(const std::string& key) const`

Retorna o valor da chave, ou `""` se não existir.

#### `void set(const std::string& key, const std::string& value)`

Armazena ou sobrescreve um valor na sessão.

#### `void erase(const std::string& key)`

Remove uma chave (ex: logout limpa `"user_id"`).

#### `std::time_t expiresAt() const`

Retorna o timestamp Unix de expiração.

#### `void touch(int ttlSeconds)`

**O que faz:** `expiresAt_ = time(0) + ttlSeconds`. "Renova" a sessão — cada requisição do usuário estende o prazo.

**Por que:** sessões que o usuário está usando ativamente não devem expirar. Só expiram se ficarem inativas por `ttlSeconds` (padrão: 3600 = 1 hora).

#### `bool expired(std::time_t now) const`

**O que faz:** `expiresAt_ != 0 && now >= expiresAt_`. Usado pelo `SessionStore::gc()` para limpeza.

---

### `SessionStore.hpp` / `SessionStore.cpp`

**Propósito:** mapa centralizado de todas as sessões ativas. Existe um único `SessionStore` por servidor (membro do `Server`).

#### `Session& getOrCreate(const std::string& id)`

**O que faz:**
1. Se `id` está vazio → chama `generateId()` para criar um novo ID.
2. Se a sessão com aquele ID já existe → `touch(ttlSeconds_)` (renova) e retorna referência.
3. Se não existe → cria nova `Session(id)`, insere no mapa, retorna referência.

**Por que retornar referência:** o `Router::attachSessionCookie()` vai chamar `session.set("user_id", "42")` e essas mudanças precisam persistir no mapa. Retornar por valor perderia as mudanças.

#### `Session* find(const std::string& id)`

**O que faz:** busca no mapa sem criar. Retorna `nullptr` se não existir.

**Quando usar:** quando você quer verificar se a sessão existe sem renovar o TTL (ex: validação inicial antes do `getOrCreate`).

#### `void drop(const std::string& id)`

Remove a sessão do mapa (logout explícito).

#### `void gc()`

**O que faz:** percorre o mapa e remove todas as sessões com `expired(now) == true`.

**Quando chamado:** pelo `EventLoop::runOnce()` a cada tick. Como a sessão tem TTL de 1h e o tick é de 1s, o GC faz pouco trabalho por chamada — eficiente.

**Por que não remover dentro do `for`:** em C++98, modificar um `std::map` durante iteração invalida o iterador. O padrão correto (já implementado no stub) é salvar o iterador a deletar antes de incrementar.

#### `void setTtlSeconds(int ttl)` / `int ttlSeconds() const`

Permite configurar o TTL pelo `main()` ou pelo arquivo de config. Padrão: 3600 segundos.

#### `(private) generateId()`

**O que faz no stub:** gera um ID pseudo-aleatório baseado em um contador e operações de bit.

**O que o Membro 3 deve implementar:** leitura de `/dev/urandom` para gerar 16 bytes aleatórios e convertê-los para hexadecimal. IDs previsíveis são uma vulnerabilidade de segurança (session fixation).

---

## 11. Divisão de Ownership

| Arquivo | Dono | Fase |
|---------|------|------|
| `common/Types.cpp` | M2 | 0 (feito) |
| `common/HttpStatus.cpp` | M2 | 0 (feito) |
| `common/FileDescriptor.cpp` | M2 | 0 (feito) |
| `common/Socket.cpp` | M1 | 1 |
| `common/Logger.cpp` | M2 | 0 (feito) |
| `common/StringUtils.cpp` | M2 | 0 (feito) |
| `config/ConfigParser.cpp` | M2 | 1 |
| `config/ServerConfig.cpp` | M2/M3 | 1 |
| `config/LocationConfig.cpp` | M2 | 0 (feito) |
| `core/EventLoop.cpp` | M1 | 1 |
| `core/Server.cpp` | M1 | 1 |
| `core/Client.cpp` | M1 | 1 |
| `http/Request.cpp` | M2 | 0 (feito) |
| `http/RequestParser.cpp` | M2 | 1 |
| `http/Response.cpp` | M3 | 1 |
| `http/ResponseFactory.cpp` | M3 | 1 |
| `http/MimeTypes.cpp` | M2 | 0 (feito) |
| `http/Router.cpp` | M3 | 1 |
| `http/handlers/GetHandler.cpp` | M3 | 1 |
| `http/handlers/PostHandler.cpp` | M3 | 1 |
| `http/handlers/DeleteHandler.cpp` | M3 | 1 |
| `cgi/CgiEnv.cpp` | M3 | 2 (integração) |
| `cgi/CgiHandler.cpp` | M1 | 2 (integração) |
| `session/Session.cpp` | M3 | 1 |
| `session/SessionStore.cpp` | M3 | 1 |

---

## 12. Restrições do Subject que moldaram a Arquitetura

| Restrição | Onde foi tratada |
|-----------|-----------------|
| Apenas um `poll()` em todo o projeto | `EventLoop::runOnce()` — único ponto |
| Nunca ler/escrever sem `poll()` indicar | `IPollable::onReadable()`/`onWritable()` só são chamados após `poll()` |
| `fork()` apenas para CGI | `CgiHandler::start()` |
| Sem threads | Single-thread by design; `SessionStore` sem locks |
| Sem `errno` após operações I/O | Verificar retorno das syscalls; `errno` só antes do próximo syscall |
| Sem vazamento de memória | RAII em `FileDescriptor`; `reapClosed()` deleta `Client`s |
| Sem vazamento de FD | RAII em `FileDescriptor`; `wantsClose()` garante fechamento |
| C++98 strict | Sem `nullptr`, sem `auto`, sem range-for, sem `std::to_string`, sem smart pointers |
| `-Wall -Wextra -Werror` | Sem variáveis não utilizadas (por isso os parâmetros de TODO são `/*comentados*/`) |
