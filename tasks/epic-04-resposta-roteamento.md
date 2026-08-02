# Épico 04 — Resposta HTTP e Roteamento

> **Dono primário:** Membro 3 (M3)
> **Branch:** `feat/http-logic`
> **Valor entregue:** capacidade de transformar uma `Request` parseada em uma `Response` HTTP válida, com routing por `LocationConfig`, geração de respostas de erro customizadas, redirects e serialização final em bytes prontos para o socket.
> **Critério de "épico pronto":** dado um `Request` mockado, o `Router::route()` retorna uma `Response` correta para cada cenário (200, 301, 302, 400, 403, 404, 405); `Response::toString()` gera o output exato esperado por `curl -v`.

> **Status do épico (auditoria de 02/08/2026):** 🟢 **7 ✅ / 0 ⚠️ / 3 ❌** — o núcleo está
> fechado e verificado no smoke test (200/301/403/404/405 corretos). Falta o suporte a
> cookies (T08, pré-requisito do bônus), os headers `Date`/`Server` (T10, nova) e a
> cobertura de testes (T09, reescopada).
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

---

## ✅ E04-T01 — Implementar `Response::toString` (serialização HTTP)

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Response.cpp`, `include/http/Response.hpp`
- **Dependências:** nenhuma (usa apenas `common/HttpStatus`)
- **Descrição:** serializar `Response` no formato `HTTP/1.1 <status> <reason>\r\n<headers>\r\n\r\n<body>`. Usar `statusReason(code)` para a frase. Cada header é `Key: Value\r\n`. Body é appended cru após o `\r\n` separador.
- **Critérios de aceite:**
  - [x] Output de `Response(200)` com body `"hello"` é exatamente `HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello`.
  - [x] Headers com `\r\n` ou caracteres de controle no valor são sanitizados; nomes inválidos são descartados com log — proteção real contra header injection (`isValidHeaderKey` + `sanitizeHeaderValue`).
  - [x] `Content-Length` reflete `body.size()` automaticamente, e `setHeader("Content-Length", ...)` é **rejeitado** para não conflitar com `setBody()`.
  - [x] Headers são emitidos em ordem determinística (ordem do `std::map`).
  - [x] Suporta múltiplos `Set-Cookie` — armazenados num `StringVec` separado (`cookies_`) e emitidos um por linha, resolvendo a limitação do `std::map`.

> **Nota:** este `toString()` **não** emite `Date` nem `Server`, e não anuncia
> `Connection: close`. Nenhum critério desta tarefa pedia isso — mas E08-T04 (browsers) pede
> `Date`. Coberto pela nova [E04-T10](#-e04-t10--emitir-date-server-e-connection-close-na-resposta).

---

## ✅ E04-T02 — Implementar `ResponseFactory::makeError` (com error_pages customizadas)

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`, `include/http/ResponseFactory.hpp`
- **Dependências:** E04-T01
- **Descrição:** verificar se `cfg.errorPages[code]` existe e o arquivo é legível → ler conteúdo e usar como body com `Content-Type: text/html`. Se não existir, gerar página HTML embutida genérica.
- **Critérios de aceite:**
  - [x] `errorPages[404] = "/errors/404.html"` faz a resposta 404 conter o conteúdo do arquivo. ← verificado no smoke test
  - [x] Se o arquivo configurado não existir → fallback para página embutida, com log de erro (sem exceção).
  - [x] `Content-Type: text/html; charset=utf-8` é setado.
  - [x] Status reason bate com `statusReason()`.
  - [x] Funciona para todos os códigos de erro suportados.

> **Detalhe:** o `error_page` é resolvido como **URI**, não como path do filesystem —
> `findRootForUri()` faz longest-prefix nas locations para achar o `root` certo, com
> fallback para o path literal. É o comportamento do Nginx e evita que `/errors/404.html`
> seja lido de `/errors/` no disco.

---

## ✅ E04-T03 — Implementar `ResponseFactory::makeRedirect`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** criar `Response` com `status_code = code` (default 302), `Location: url`, body vazio. Alimentado pela diretiva `return <code> <url>;` do `.conf` (ver [BUG-02-02](epic-02-parser-configuracao.md#bug-02-02--a-diretiva-de-redirect-chama-se-return-mas-a-documentação-diz-redirect) — a diretiva é `return`, o campo do struct é `redirect`).
- **Critérios de aceite:**
  - [x] `makeRedirect("https://example.com", 301)` produz `301 Moved Permanently` com `Location: https://example.com`.
  - [x] Default `code = 302` quando omitido.
  - [x] Body vazio (`Content-Length: 0`).
  - [x] Apenas 301 e 302 são aceitos; outros geram 500 com log. ← funciona, mas seria melhor barrar isso no parsing da config (ver [BUG-02-01](epic-02-parser-configuracao.md#bug-02-01--três-validações-semânticas-de-e02-t05-não-foram-implementadas), item 2)
  - [x] URL vazia gera 500 com log em vez de um `Location:` quebrado.

---

## ✅ E04-T04 — Implementar `ResponseFactory::makeFile`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** abrir `fsPath` em modo binário, ler conteúdo completo para string, criar `Response(200)` com `Content-Type: mime`, `Content-Length`, body = conteúdo. Em erro de I/O, mapear para o status adequado.
- **Critérios de aceite:**
  - [x] Arquivo binário é lido sem corrupção (`std::ios::binary`).
  - [x] Arquivo de 0 bytes gera `200 OK` com `Content-Length: 0`.
  - [x] Arquivo grande é lido em uma operação (aceitável para o subject).
  - [x] Falha de leitura vira `Response` de erro, não exceção — e o status é **discriminado**: inexistente → 404, sem permissão ou não-regular → 403, falha de I/O → 500 (via `readRegularFile`).

---

## ✅ E04-T05 — Implementar `ResponseFactory::makeAutoindex`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** abrir o diretório `fsPath` com `opendir()`, listar entradas com `readdir()`, gerar HTML simples com `<ul>` de `<a href>` apontando para `uriPath/<entry>`.
- **Critérios de aceite:**
  - [x] HTML válido, com `<!DOCTYPE html>` e `<meta charset>`.
  - [x] Links são URLs relativas baseadas em `uriPath`, não caminhos do filesystem — e passam por `PathResolver::encodeSegment` (percent-encoding de nomes com espaço/acento).
  - [x] Diretórios aparecem com `/` no fim.
  - [x] Ordenação alfabética (`std::sort`).
  - [x] `Content-Type: text/html; charset=utf-8`.
  - [x] Diretório vazio gera HTML válido com lista vazia.
  - [x] Nomes de arquivo são escapados (`escapeHtml`) — proteção contra XSS armazenado via nome de arquivo em `upload_store`.

---

## ✅ E04-T06 — Implementar `Router::route` (dispatch principal)

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Router.cpp`, `include/http/Router.hpp`
- **Dependências:** E04-T02, E04-T03, E02-T06, handlers do Épico 05
- **Descrição:** orquestrar o roteamento:
  1. `loc = vhost.findLocation(req.path())` — se `NULL` → `makeError(404, vhost)`.
  2. Se `loc->redirect` não vazio → `makeRedirect(loc->redirect, loc->redirectCode)`.
  3. Se `!methodAllowed(req.method(), *loc)` → `makeError(405, vhost)` com header `Allow`.
  4. Despachar para handler conforme método (`getH_`, `postH_`, `deleteH_`).
  5. Retornar a `Response`.
- **Critérios de aceite:**
  - [x] `GET /` em config sem `/` location → 404. ← verificado
  - [x] `PUT /` em location com `methods GET` → 405 com header `Allow`. ← verificado
  - [x] `GET /old` com `return /new` → redirect com `Location: /new`.
  - [x] Despacho correto para `GetHandler`, `PostHandler`, `DeleteHandler` por método.
  - [x] Método desconhecido (TRACE, OPTIONS) → 405 com `Allow: GET, POST, DELETE`.
  - [x] Qualquer exceção escapando de um handler vira 500 em vez de derrubar o servidor (`try/catch` envolvendo todo o dispatch).

> **Ponto de extensão:** o dispatch de CGI **não** passa por aqui — hoje ele está enterrado
> dentro do `PostHandler`, o que faz `GET` em script `.py` servir o código-fonte. Corrigido
> pela nova [E06-T09](epic-06-cgi.md#-e06-t09--detectar-cgi-no-routerroute-get-e-post).

---

## ✅ E04-T07 — Implementar `Router::methodAllowed`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/http/Router.cpp`
- **Dependências:** E04-T06
- **Descrição:** verificar se `method` está em `loc.methods`. Se `loc.methods` está vazio, considerar todos os métodos permitidos (comportamento Nginx).
- **Critérios de aceite:**
  - [x] `methods = ["GET", "POST"]` aceita GET/POST e rejeita DELETE.
  - [x] `methods = []` (não declarado) aceita todos — decisão documentada, igual ao Nginx. O header `Allow` nesse caso lista `GET, POST, DELETE`.
  - [x] Comparação tolerante a caixa (`StringUtils::iequals`), com os métodos já normalizados para maiúsculas pelo `ConfigParser` (E02-T04).

---

## ❌ E04-T08 — Implementar `Request::cookie` (leitura de cookies)

- **Owner:** M3
- **Status:** ❌ PENDENTE — stub retornando string vazia (`src/http/Request.cpp:35-38`)
- **Tamanho:** S
- **Arquivos afetados:** `src/http/Request.cpp`
- **Dependências:** E03-T03
- **Descrição:** parsear o header `Cookie: name1=val1; name2=val2` e retornar o valor da chave `name`. Se não existir, retornar string vazia.
- **Critérios de aceite:**
  - [ ] `Cookie: a=1; b=2` → `cookie("a") == "1"`, `cookie("b") == "2"`.
  - [ ] Espaços ao redor de `;` e do `=` são ignorados.
  - [ ] Cookie não existente → `""`.
  - [ ] Nomes case-sensitive (cookies são case-sensitive por RFC 6265) — atenção: o `HeaderMap` é case-**insensitive** para achar o header `Cookie`, mas a chave do cookie dentro do valor não é.
  - [ ] Múltiplos headers `Cookie` são tratados. ← **limitação conhecida**: o `HeaderMap` é um `std::map`, então o `RequestParser` já colapsou headers duplicados (o último vence). Na prática os clientes mandam um único header `Cookie`; documentar a limitação e seguir.

> **Bloqueia:** [E07-T06](epic-07-bonus-sessoes.md) (`Router::attachSessionCookie`).

---

## ❌ E04-T09 — Cobertura de `ResponseFactory` e `Router` no `curl-suite.sh`

- **Owner:** M3
- **Status:** ❌ PENDENTE — **reescopada em 02/08/2026** (era: "Testes unitários de `ResponseFactory` e `Router`" em `tests/unit/test_response_factory.cpp`)
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/curl-suite.sh`, `tests/configs/basic.conf`
- **Dependências:** E04-T01–E04-T08
- **Motivo do reescopo:** o padrão de teste do projeto é `curl-suite.sh` + `test-edge-cases.sh` (ver a política em [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md)). Sem testes unitários em C++.
- **Descrição:** cobrir cada fábrica e cada ramo do `Router` por HTTP real, usando uma config de teste que exercite todos os caminhos (location com `return`, location sem `methods`, location com `autoindex on`, `error_page` customizada).
- **Critérios de aceite:**
  - [ ] Status codes verificados via `curl -o /dev/null -w "%{http_code}"`: 200, 301 (dir sem barra), 302/301 (`return`), 403 (autoindex off), 404, 405 (`PUT`).
  - [ ] Header `Allow` presente e correto na resposta 405 (`curl -sI | grep -i '^Allow:'`).
  - [ ] Header `Location` correto no redirect.
  - [ ] `error_page` customizada: o body do 404 contém uma string exclusiva de `www/errors/404.html`.
  - [ ] Autoindex: com `autoindex on`, o body contém `<title>Index of` e um `<a href>` para um arquivo conhecido.
  - [ ] `Content-Type` correto para `.html`, `.png` e extensão desconhecida (`application/octet-stream`).

---

## ❌ E04-T10 — Emitir `Date`, `Server` e `Connection: close` na resposta

- **Owner:** M3
- **Status:** ❌ NOVA — criada em 02/08/2026 na auditoria
- **Tamanho:** S
- **Arquivos afetados:** `src/http/Response.cpp`, `src/core/Client.cpp`
- **Dependências:** E04-T01
- **Motivo:** E04-T01 cumpre todos os critérios que foram escritos para ela, mas o escopo
  ficou curto: E08-T04 exige `Date` para a validação em browsers, e hoje uma resposta que
  fecha a conexão (`closeAfterWrite_`, erro de parsing ou `Connection: close` do cliente)
  encerra o socket **sem anunciar** `Connection: close`, o que obriga o cliente a descobrir
  o fim pelo FIN. Escopo não previsto → tarefa nova, não bug.
- **Descrição:** acrescentar em `Response::toString()` os headers `Date` (formato IMF-fixdate
  da RFC 7231 §7.1.1.1, sempre em GMT) e `Server: webserv`, ambos só se ainda não presentes.
  O `Connection` é decidido pelo `Client`, que já sabe se vai fechar — expor um
  `setHeader("Connection", ...)` no ponto onde `closeAfterWrite_`/`keepAlive()` são avaliados.
- **Critérios de aceite:**
  - [ ] Toda resposta traz `Date:` no formato `Sun, 02 Aug 2026 17:10:00 GMT` (via `std::strftime` com `"%a, %d %b %Y %H:%M:%S GMT"` sobre `gmtime`).
  - [ ] O locale não afeta o formato — o mês e o dia da semana têm que sair em inglês. Se `strftime` depender de locale no ambiente, usar tabelas próprias de 7 e 12 strings.
  - [ ] Toda resposta traz `Server: webserv`.
  - [ ] Resposta que fecha a conexão traz `Connection: close`; resposta em keep-alive traz `Connection: keep-alive`.
  - [ ] Um header `Date`/`Server` vindo de um script CGI (E06-T06) **não** é sobrescrito.
  - [ ] `curl -sI http://localhost:8080/` mostra os três headers.

---

## Resumo de tarefas

| ID | Tarefa | Status | Tamanho | Dependências |
|----|--------|--------|---------|-------------|
| E04-T01 | Response::toString | ✅ | M | — |
| E04-T02 | makeError | ✅ | M | T01 |
| E04-T03 | makeRedirect | ✅ | S | T01 |
| E04-T04 | makeFile | ✅ | M | T01 |
| E04-T05 | makeAutoindex | ✅ | M | T01 |
| E04-T06 | Router::route | ✅ | M | T02, T03, E02-T06, Épico 05 |
| E04-T07 | methodAllowed | ✅ | S | T06 |
| E04-T08 | Request::cookie | ❌ | S | E03-T03 |
| E04-T09 | Cobertura no curl-suite.sh | ❌ reescopada | M | T01–T08 |
| E04-T10 | Date, Server, Connection | ❌ nova | S | T01 |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`.

**Nenhum bug aberto neste épico.** Tudo o que foi implementado cumpre os próprios critérios
de aceite — as duas lacunas encontradas viraram tarefas novas em vez de bugs, porque o
escopo original nunca as previu:

- **`Date`/`Server`/`Connection`** → [E04-T10](#-e04-t10--emitir-date-server-e-connection-close-na-resposta) (nesta página).
- **Dispatch de CGI para GET** → [E06-T09](epic-06-cgi.md#-e06-t09--detectar-cgi-no-routerroute-get-e-post).
  O `Router::route` desta página é o lugar certo para a detecção; hoje ela está dentro do
  `PostHandler`, e o sintoma está registrado como
  [BUG-05-01](epic-05-handlers-http.md#bug-05-01--get-em-script-cgi-devolve-o-código-fonte).
