# Membro 3 — Lógica HTTP (`Router`, handlers, `ResponseFactory`, sessões, `CgiEnv`)

> Você decide **o que** responder: qual location, qual método, qual arquivo, qual
> status code. Visão geral em [README.md](README.md).

**Arquivos:** [Router](../../src/http/Router.cpp) · [PathResolver](../../src/http/PathResolver.cpp) ·
[GetHandler](../../src/http/handlers/GetHandler.cpp) · [PostHandler](../../src/http/handlers/PostHandler.cpp) ·
[DeleteHandler](../../src/http/handlers/DeleteHandler.cpp) · [ResponseFactory](../../src/http/ResponseFactory.cpp) ·
[Response](../../src/http/Response.cpp) · [MimeTypes](../../src/http/MimeTypes.cpp) ·
[SessionStore](../../src/session/SessionStore.cpp) · [CgiEnv](../../src/cgi/CgiEnv.cpp)

---

## 1. `Router::route()` — a ordem das decisões

[src/http/Router.cpp:58](../../src/http/Router.cpp#L58)

```cpp
findLocation(path)          → 0?            → 404
location tem `return`?                      → 301/302        // antes de tudo
método permitido?           → não           → 405 + Allow
path == "/session"?                         → bônus, sem tocar no disco
location tem `cgi` e a extensão casa?       → prepareCgi()    // antes do método
método == GET / POST / DELETE               → handler
                                            → 405 + Allow (fallback)
```

**A ordem é a defesa.** Cada passo tem um porquê:

- **`return` antes do método**: uma location que só redireciona não precisa listar
  métodos; o Nginx se comporta igual.
- **405 antes de olhar o disco**: não faz sentido procurar arquivo para um método
  que a rota não aceita. O header `Allow` sai da própria diretiva `methods`
  ([:35](../../src/http/Router.cpp#L35)), como manda a RFC 7231 §6.5.5.
- **CGI antes do despacho por método**: o mesmo script atende GET e POST; quem
  decide é a extensão, não o verbo.
- **`methods` ausente = todos permitidos** ([:137](../../src/http/Router.cpp#L137)),
  mesma semântica de "sem restrição".
- **Todo o corpo do `route()` está dentro de um `try/catch`**
  ([:94](../../src/http/Router.cpp#L94)): qualquer exceção vira 500, nunca um crash.
  A régua zera a nota por terminação inesperada — este é o cinto de segurança.

`prepareCgi()` ([:103](../../src/http/Router.cpp#L103)) não executa nada: resolve o
caminho, valida e preenche o `CgiTarget`. Quem faz `fork()` é o M1.
`CgiTarget::active()` é o sinal para o `Client` de que a `Response` devolvida deve
ser descartada.

Duas decisões defensáveis aqui:

- **Diretório vira 403, mas arquivo ausente não vira 404.** O programa CGI é o
  handler da extensão e decide sozinho o que fazer com um alvo que não existe — o
  `cgi_tester` da avaliação, por exemplo, responde 200 sem nunca abrir o arquivo. A
  rede de segurança fica no `CgiHandler::buildResponse()`, que converte o 502 em 404
  quando o script não produziu resposta **e** o alvo realmente não existe. É assim
  que um `.py` inexistente continua respondendo 404 em vez de 502.
- **`access(X_OK)` no interpretador** ([:125](../../src/http/Router.cpp#L125)) → 500
  com log explícito. Sem isso, um `cgi` mal configurado aparece só como um 502 mudo.

---

## 2. `PathResolver` — URL → caminho em disco (e a segurança)

[src/http/PathResolver.cpp:141](../../src/http/PathResolver.cpp#L141)

```
percentDecode  →  normalizePath  →  escolhe root  →  strip do prefixo  →  join
```

- **`percentDecode`** ([:16](../../src/http/PathResolver.cpp#L16)): `%XX` inválido →
  400; `%00` é rejeitado (truncaria o caminho no `open`).
- **`normalizePath`** ([:38](../../src/http/PathResolver.cpp#L38)): resolve `.` e
  `..`; se um `..` tentar subir acima da raiz, devolve falso → **403**. É a defesa
  contra path traversal (`GET /../../etc/passwd`). Decodificar **antes** de
  normalizar é o que impede o bypass com `%2e%2e%2f`.
- **Escolha do root** ([:157](../../src/http/PathResolver.cpp#L157)): a location tem
  `root`? usa o dela e remove o prefixo da location do caminho. Senão usa o do
  server e mantém o caminho inteiro.

É este par que implementa o exemplo do subject: `/kapouet` com `root /tmp/www` faz
`/kapouet/pouic/toto/pouet` → `/tmp/www/pouic/toto/pouet`.

---

## 3. Os três handlers

### GET ([GetHandler.cpp:13](../../src/http/handlers/GetHandler.cpp#L13))

```
resolve → stat
  ├── é diretório?
  │      ├── URL sem "/" final  → 301 para a URL com "/" (preserva a query)
  │      ├── existe o `index`?  → serve o arquivo
  │      ├── autoindex on?      → listagem gerada
  │      └── senão              → 404
  ├── é arquivo regular         → 200 + Content-Type por extensão
  ├── não existe                → 404
  └── outro tipo (socket, fifo) → 403
```

O redirect do diretório sem barra importa: sem ele, os links relativos da página
quebrariam. O `autoindex` da location vence o do server, e a ausência da diretiva
herda ([:61](../../src/http/handlers/GetHandler.cpp#L61)).

**Por que 404 e não 403 no último caso** ([:67](../../src/http/handlers/GetHandler.cpp#L67)):
o Nginx responde 403 ali, nós respondemos 404. Sem index e sem listagem não existe
recurso algum naquela URI, e o 403 ainda vazaria a informação de que o diretório
existe. É uma escolha, não um descuido — saiba defender as duas leituras.

### POST / upload ([PostHandler.cpp:159](../../src/http/handlers/PostHandler.cpp#L159))

```
sem upload_store?                          → 403 (erro de configuração da rota)
upload_store não gravável?                 → 500
Content-Type multipart/form-data?          → extrai boundary + primeira parte
nome do arquivo: multipart → URI → gerado  → sanitizeFilename()
grava e responde                           → 201 + Content-Location + página HTML
```

- **`sanitizeFilename`** ([:13](../../src/http/handlers/PostHandler.cpp#L13)) reduz
  ao basename e rejeita `.`, `..` e caracteres de controle: o cliente não escolhe
  onde grava, só o nome.
- **Sem cópia desnecessária**: `content` aponta para `req.body()` quando não há
  multipart ([:187](../../src/http/handlers/PostHandler.cpp#L187)) — copiar até 10 MB
  por upload seria desperdício.
- **201 com corpo HTML** para o navegador não ficar numa página em branco depois do
  submit; o nome do arquivo passa por `escapeHtml` antes de entrar no HTML.

### DELETE ([DeleteHandler.cpp:46](../../src/http/handlers/DeleteHandler.cpp#L46))

Resolve em `upload_store` quando a location tem um (para ser simétrico com o POST) e
pelo `root` quando não tem. Só apaga arquivo regular (senão 403), exige permissão de
escrita no diretório-pai (403) e responde **204 No Content**.

> O teste `DELETE forbidden` do `curl-suite` tira a permissão de escrita do
> diretório e confirma o 403 real — vale mostrar na defesa.

---

## 4. `ResponseFactory` e `Response`

[ResponseFactory.cpp](../../src/http/ResponseFactory.cpp) — cinco construtores de resposta:

| Função | Uso |
|---|---|
| `makeError(code, cfg, loc)` | `error_page` da location → do server → **página embutida** |
| `makeFile` | arquivo estático com MIME |
| `makeAutoindex` | listagem de diretório |
| `makeRedirect` | 301/302 (`Location`) |
| `makeFromCgi` | traduz a saída do script em resposta HTTP |

**Página de erro padrão** ([:105](../../src/http/ResponseFactory.cpp#L105)): o
subject exige que exista uma quando nenhuma é configurada. Se a `error_page`
configurada não puder ser lida, logamos e caímos na embutida — um `error_page`
quebrado nunca vira 500.

**Precedência da `error_page`** ([:67](../../src/http/ResponseFactory.cpp#L67)): a da
location vence a do server; se a location não define aquele código, herda. Por isso
todos os handlers passam `&loc` para o `makeError`.

**`makeFromCgi`** ([:286](../../src/http/ResponseFactory.cpp#L286)) implementa a
RFC 3875 §6: headers, linha em branco, body. Aceita `\r\n\r\n` e `\n\n` (scripts
Python costumam usar `\n`), traduz o header `Status:` em status code e devolve
**502** se a saída não tiver bloco de headers válido — é o caminho que `broken.py` e
`exit.py` demonstram.

**`Response`** ([Response.cpp](../../src/http/Response.cpp)):

- `setBody()` recalcula `Content-Length` — nunca setar o header na mão
  ([:82](../../src/http/Response.cpp#L82)); `setHeader("Content-Length", …)` é
  ignorado de propósito.
- `setHeader` valida o nome e sanitiza o valor: um `\r\n` vindo do CGI ou de um nome
  de arquivo seria **response splitting**.
- `Date` no formato IMF-fixdate, em inglês e GMT, escrito à mão porque `strftime`
  depende de locale ([:11](../../src/http/Response.cpp#L11)).
- `Set-Cookie` é o único header que pode repetir, então vive num vetor separado.

---

## 5. CGI: as variáveis de ambiente

[CgiEnv.cpp:33](../../src/cgi/CgiEnv.cpp#L33) — RFC 3875. O subject cobra que "a
requisição completa e os argumentos do cliente estejam disponíveis para o CGI":

```
GATEWAY_INTERFACE=CGI/1.1     REQUEST_METHOD    CONTENT_LENGTH
SERVER_SOFTWARE=webserv/1.0   REQUEST_URI       CONTENT_TYPE
SERVER_PROTOCOL=HTTP/1.1      QUERY_STRING      REDIRECT_STATUS=200  (php-cgi)
SERVER_NAME / SERVER_PORT     SCRIPT_NAME       PATH_INFO       = caminho da URI
                              SCRIPT_FILENAME   PATH_TRANSLATED = caminho em disco
+ todo header da requisição vira HTTP_<NOME>, com '-' → '_' e maiúsculas
```

**`PATH_INFO` é o ponto sensível.** Pela RFC 3875 ele seria vazio aqui, já que a URI
termina no próprio script. Mandamos o caminho da URI porque o `cgi_tester` da
avaliação recusa rodar sem ele (`500 PATH_INFO not found`) e valida o valor contra
`SCRIPT_NAME`/`REQUEST_URI` — mandar o caminho em disco dá `PATH_INFO incorrect`.
Esse caminho em disco vai em `PATH_TRANSLATED`, que é onde a RFC o quer.

`Content-Length` e `Content-Type` saem **sem** o prefixo `HTTP_`, como manda a RFC, e
por isso são pulados no laço ([:61](../../src/cgi/CgiEnv.cpp#L61)). Demonstração ao
vivo: `cgi-bin/env_dump.py` imprime o ambiente recebido e `cgi-bin/post_echo.py`
devolve o body que chegou pelo stdin.

---

## 6. Bônus: sessões e cookies

[Router::handleSession](../../src/http/Router.cpp#L149) + [SessionStore](../../src/session/SessionStore.cpp)

```
GET /session
  ├── tem cookie `sid` conhecido? → contador++ e `touch()` (renova o TTL)
  └── não                         → cria sessão + `Set-Cookie: sid=…; Path=/; HttpOnly`
```

O estado fica **no servidor** (`std::map<id, Session>`); o browser só carrega o
identificador. O GC roda a cada tick do `EventLoop`
([`SessionStore::onTick`](../../src/session/SessionStore.cpp#L30)), removendo sessões
paradas há mais de 1 hora — sem isso o mapa cresceria para sempre sob siege.

Demonstração: `curl -c jar http://…/session` e depois `curl -b jar …` (contador
sobe); no navegador, aba Network mostrando o `Set-Cookie` e o `Cookie` do reload.

> Honestidade: o id é 32 hex derivados de relógio, contador e `rand()`
> ([:51](../../src/session/SessionStore.cpp#L51)) — bom para demonstrar o mecanismo,
> não para autenticação real. Diga isso antes de perguntarem.

---

## 7. Perguntas que vão cair em cima de você

**"Como vocês escolhem a location?"**
`findLocation()` faz longest-prefix match: `/upload/a.txt` casa com `/upload`, não
com `/`. Sem match, 404.

**"Por que 405 e não 501 para `PUT`?"**
A location declara quais métodos aceita; `PUT` não está na lista, então "método não
permitido nesta rota" (405) é mais preciso que "não implementado" (501). Mandamos o
header `Allow` junto, como a RFC exige.

**"Um `..` na URL consegue sair do root?"**
Não. Decodificamos o percent-encoding, normalizamos e recusamos com 403 se sobrar
`..` acima da raiz. Teste: `curl --path-as-is http://…/../../etc/passwd`.

**"E se a `error_page` configurada não existir?"**
Log de erro e página embutida com o mesmo status. Nunca vira 500.

**"O que acontece se o script CGI imprimir lixo?"**
`makeFromCgi` não acha bloco de headers válido e devolve 502. O servidor continua
no ar — `curl http://…/cgi-bin/exit.py` e depois `curl http://…/` demonstram.

**"Onde vocês mataram a duplicação entre GET e CGI?"**
Os dois passam pelo mesmo `PathResolver::resolve()`; a diferença é só o que se faz
com o caminho resolvido (ler o arquivo × executar o script).
