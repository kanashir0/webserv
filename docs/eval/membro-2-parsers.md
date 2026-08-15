# Membro 2 — Parsers (`src/config/`, `RequestParser`, `src/common/`)

> Você transforma texto em estrutura: o `.conf` vira `ServerConfig`, os bytes do
> socket viram `Request`. Visão geral em [README.md](README.md).

**Arquivos:** [ConfigParser](../../src/config/ConfigParser.cpp) ·
[ServerConfig](../../src/config/ServerConfig.cpp) · [LocationConfig](../../include/config/LocationConfig.hpp) ·
[RequestParser](../../src/http/RequestParser.cpp) · [StringUtils](../../src/common/StringUtils.cpp) ·
[Types/HeaderMap](../../include/common/Types.hpp) · [HttpStatus](../../src/common/HttpStatus.cpp)

---

## 1. `ConfigParser` — do arquivo ao `vector<ServerConfig>`

[src/config/ConfigParser.cpp](../../src/config/ConfigParser.cpp)

```
parseFile(path)                       // stat() antes: diretório não é config válida
  └─ parseString(conteúdo)
       └─ doParse()                   // top level: só aceita blocos `server`
            └─ parseServerBlock()     // listen, server_name, root, index,
                 │                    // autoindex, client_max_body_size, error_page
                 └─ parseLocationBlock()  // methods, root, index, autoindex,
                                          // error_page, return, upload_store,
                                          // client_max_body_size, cgi
```

O tokenizador ([:169](../../src/config/ConfigParser.cpp#L169)) devolve `{`, `}`, `;`
como tokens próprios, ignora comentários `#` e conta linhas para a mensagem de erro.
String vazia = EOF, porque nenhum token válido é vazio.

**Toda falha vira `ParseError(msg, linha)` e mata o processo no startup**, com o
formato que o `main` imprime ([main.cpp:40](../../src/main.cpp#L40)):

```
[ERROR] conf/invalid/invalid_port.conf:3: port out of range: '-5'
```

### Validações (uma config inválida por regra em `conf/invalid/`)

| Regra | Onde | Arquivo de teste |
|---|---|---|
| porta 1–65535 | [:73](../../src/config/ConfigParser.cpp#L73) | `invalid_port.conf` |
| host IPv4 válido no `listen` | [:68](../../src/config/ConfigParser.cpp#L68) | `invalid_host.conf` |
| `methods` só GET/POST/DELETE | [:316](../../src/config/ConfigParser.cpp#L316) | `invalid_methods.conf` |
| `autoindex` só on/off | [:44](../../src/config/ConfigParser.cpp#L44) | `invalid_autoindex.conf` |
| `return` só 301/302, e código exige destino | [:341](../../src/config/ConfigParser.cpp#L341) | `invalid_return.conf` |
| `root`/`upload_store` precisam existir e ser diretório | [:35](../../src/config/ConfigParser.cpp#L35) | `invalid_root.conf`, `invalid_upload_store.conf` |
| `server_name` no charset de hostname | [:107](../../src/config/ConfigParser.cpp#L107) | `invalid_server_name.conf` |
| diretiva duplicada no mesmo bloco | [:207](../../src/config/ConfigParser.cpp#L207) | `duplicate_*.conf` (8 arquivos) |
| `location` com path repetido | [:246](../../src/config/ConfigParser.cpp#L246) | `duplicate_location.conf` |
| extensão `cgi` repetida | [:368](../../src/config/ConfigParser.cpp#L368) | `duplicate_cgi_ext.conf` |
| `;` faltando, bloco não fechado, diretiva desconhecida | [:200](../../src/config/ConfigParser.cpp#L200), [:286](../../src/config/ConfigParser.cpp#L286) | `missing_semicolon.conf`, `unclosed_block.conf`, `invalid_directive.conf` |
| nenhum bloco `server` | [:225](../../src/config/ConfigParser.cpp#L225) | `empty_file.conf` |

> **Por que validar tudo no startup?** Um `root` inexistente descoberto na primeira
> requisição vira um 404 confuso; descoberto no startup vira uma mensagem com
> arquivo e linha. Também impede o processo de subir "vivo mas mudo", sem listener.

### Herança server → location

| Diretiva | server | location | Como herda |
|---|---|---|---|
| `root`, `index` | ✅ | ✅ | string vazia = herda |
| `autoindex` | ✅ | ✅ | `autoindexSet == false` = herda |
| `client_max_body_size` | ✅ | ✅ | `clientMaxBodySizeSet == false` = herda |
| `error_page` | ✅ | ✅ | código ausente no mapa = herda |
| `listen`, `server_name` | ✅ | ❌ | — |
| `methods`, `return`, `cgi`, `upload_store` | ❌ | ✅ | — |

**O detalhe que vale explicar:** `false` e `0` são valores legítimos, então
`autoindex` e `client_max_body_size` não têm sentinela natural — carregam um
booleano `...Set` dizendo se a diretiva apareceu
([LocationConfig.hpp:19-26](../../include/config/LocationConfig.hpp#L19-L26)).
E **`client_max_body_size 0` significa "sem limite"**, nos dois níveis, coerente com
o que o `RequestParser` faz com `maxBody == 0`.

`ServerConfig::findLocation()` ([ServerConfig.cpp:19](../../src/config/ServerConfig.cpp#L19))
faz **longest-prefix match**, como o Nginx: `/upload/img.png` casa com `/upload`, não
com `/`. Também é `ServerConfig` quem guarda os defaults
(`root ./www`, `index index.html`, `client_max_body_size 1m`).

`parseSize` ([:80](../../src/config/ConfigParser.cpp#L80)) aceita sufixos `k`/`m`/`g`
case-insensitive.

---

## 2. `RequestParser` — bytes que chegam picados viram `Request`

[src/http/RequestParser.cpp](../../src/http/RequestParser.cpp)

O TCP não respeita fronteiras de mensagem: `GET / HTTP/1.1\r\n...` pode chegar em
três `recv()` diferentes, e dois pedidos podem chegar num só. Por isso o parser é
uma máquina de estados incremental sobre um buffer acumulado:

```
METHOD/URI/VERSION ──► HEADER ──► BODY_LENGTH  ──► DONE
                          │   └──► BODY_CHUNKED ──┘
                          └──► DONE (sem body)
                       qualquer estado ──► ERROR
```

### A assinatura e o `HEADERS_READY`

```cpp
FeedResult feed(const char* data, std::size_t n, std::size_t maxBody);
// NEED_MORE | HEADERS_READY | COMPLETE | BAD_REQUEST | URI_TOO_LONG
// | BODY_TOO_LARGE | HTTP_VERSION_UNSUPPORTED
```

`HEADERS_READY` ([:91](../../src/http/RequestParser.cpp#L91)) é o ponto de
integração com o M1: o limite de body depende da location, que só é conhecida
depois de ler `Host` e path. Então o parser **pausa** ao terminar os headers e
devolve o controle; o `Client` recalcula o limite e chama `feed(NULL, 0, limite)`
para retomar. Chamar com `n == 0` só processa o que já está no buffer.

`take()` ([:100](../../src/http/RequestParser.cpp#L100)) devolve a `Request` pronta e
**rearma o estado preservando `buf_`** — é o que permite pipelining: se a próxima
requisição já veio no mesmo pacote, ela está no buffer esperando.

### Proteções (todas viram status code)

| Proteção | Limite / regra | Status |
|---|---|---|
| URI longa | 8192 bytes | 414 |
| Headers | 8 KB cumulativos, 100 headers, 16 KB antes do body | 400 |
| Request line | exatamente 3 tokens, método é `tchar`, URI começa com `/` | 400 |
| Versão | só `HTTP/1.0` e `HTTP/1.1`; outro `HTTP/x` → 505 | 400 / 505 |
| `Host` ausente em HTTP/1.1 | RFC 7230 §5.4 | 400 |
| `Content-Length` duplicado | request smuggling | 400 |
| `Content-Length` **e** `Transfer-Encoding` juntos | ambíguo, smuggling | 400 |
| `Content-Length` não numérico (`+12`, `-0`) | RFC 7230 §3.3.2 = `1*DIGIT` | 400 |
| `Transfer-Encoding` diferente de `chunked` | não suportado | 400 |
| POST sem `Content-Length` nem `chunked` | sem como saber onde o body acaba | 411 |
| Body acima do limite | checado **antes** de acumular | 413 |
| `chunk-size` inválido / > 8 dígitos hex | overflow | 400 |
| obs-fold (header continuado) | deprecado na RFC 7230 §3.2.4 | 400 |

O 413 do `Content-Length` é decidido antes de ler o body
([:245](../../src/http/RequestParser.cpp#L245)); no `chunked`, a cada chunk
([:308](../../src/http/RequestParser.cpp#L308)). Não acumulamos megabytes para só
depois recusar.

### `chunked`

[:273](../../src/http/RequestParser.cpp#L273). Lê `tamanho-hex CRLF dados CRLF` até o
chunk zero, ignora chunk extensions (`;algo`) e trailers, e só consome um chunk
quando ele está inteiro no buffer. O subject exige exatamente isso: **o servidor
desmonta o chunked antes de entregar ao CGI**, que espera EOF como fim do body.

---

## 3. `common/` — as peças que todo mundo usa

- **`HeaderMap`** ([Types.hpp](../../include/common/Types.hpp)):
  `std::map<string, string, CaseInsensitiveLess>`. A RFC 7230 diz que nomes de
  header são case-insensitive, então `Content-Length` e `content-length` são a
  mesma chave — sem isso, todo lugar que consulta header precisaria normalizar.
- **`StringUtils::parseIPv4`** ([:108](../../src/common/StringUtils.cpp#L108)):
  `inet_pton`/`inet_addr` **não estão na lista de funções autorizadas** do subject.
  Exige 4 octetos 0–255 e rejeita zeros à esquerda (`01` seria octal no
  `inet_aton`).
- **`StringUtils::toLong`** usa `strtol` e valida que a string inteira foi
  consumida — daí o `bool& ok`.
- **`escapeHtml`**: nomes de arquivo do autoindex e de uploads vêm do cliente e vão
  parar em HTML; sem escape haveria XSS refletido.
- **`HttpStatus`** ([HttpStatus.cpp](../../src/common/HttpStatus.cpp)): tabela
  código → razão, usada por `Response::toString()`. É a fonte única dos status.
- **`Logger`** ([Logger.cpp](../../src/common/Logger.cpp)): Singleton, `DEBUG/INFO`
  em stdout e `WARN/ERROR` em stderr.

---

## 4. Perguntas que vão cair em cima de você

**"O que acontece se eu mandar a requisição byte a byte pelo telnet?"**
Cada `recv()` chama `feed()`, que devolve `NEED_MORE` até a mensagem fechar. O
estado fica no parser, não na pilha — é por isso que ele é uma máquina de estados e
não uma função que lê tudo de uma vez.

**"E se eu mandar duas requisições numa conexão só?"**
`take()` preserva o buffer; o `Client` chama `tryConsumeResidual()` depois de
responder e a segunda é processada sem esperar novos bytes.

**"Onde exatamente o `client_max_body_size` da location é aplicado?"**
`Client::effectiveBodyLimit()` calcula (location vence server), e o parser usa esse
valor em `parseBodyByLength`/`parseBodyChunked`. Demonstre com `curl` de 2 MB em `/`
(413, limite 1m) e em `/upload` (201, limite 10m).

**"Por que 411 e não 400 no POST sem `Content-Length`?"**
Porque é exatamente o que o 411 Length Required significa. Sem `Content-Length` nem
`chunked` não há como saber onde o body termina.

**"Config errada derruba o servidor no meio da avaliação?"**
Não: erro de config aborta no startup, antes de qualquer socket, com arquivo e
linha. Temos 26 arquivos em `conf/invalid/`, um por regra — pode rodar qualquer um.

**"Por que não usaram uma tabela de despacho no `ConfigParser`?"**
Dívida técnica assumida e documentada: seriam ~14 funções-membro e dois mapas
contra 141 linhas que funcionam e têm cobertura de teste por regra. Ganho funcional
zero — decidimos não mexer antes da entrega.
