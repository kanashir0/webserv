# Épico 04 — Resposta HTTP e Roteamento

> **Dono primário:** Membro 3 (M3)
> **Branch:** `feat/http-logic`
> **Valor entregue:** capacidade de transformar uma `Request` parseada em uma `Response` HTTP válida, com routing por `LocationConfig`, geração de respostas de erro customizadas, redirects e serialização final em bytes prontos para o socket.
> **Critério de "épico pronto":** dado um `Request` mockado, o `Router::route()` retorna uma `Response` correta para cada cenário (200, 301, 302, 400, 403, 404, 405); `Response::toString()` gera o output exato esperado por `curl -v`.

---

## E04-T01 — Implementar `Response::toString` (serialização HTTP)

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Response.cpp`, `include/http/Response.hpp`
- **Dependências:** nenhuma (usa apenas `common/HttpStatus`)
- **Descrição:** serializar `Response` no formato `HTTP/1.1 <status> <reason>\r\n<headers>\r\n\r\n<body>`. Usar `HttpStatus::statusReason(code)` para a frase. Cada header é `Key: Value\r\n`. Body é appended cru após o `\r\n` separador.
- **Critérios de aceite:**
  - [ ] Output de `Response(200)` com body `"hello"` é exatamente `HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello`.
  - [ ] Headers com `\r\n` ou `:` no valor são rejeitados ou sanitizados (proteção contra header injection).
  - [ ] `Content-Length` reflete `body.size()` automaticamente (já garantido pelo `setBody()`).
  - [ ] Headers são emitidos em ordem determinística (ordem do `std::map` por padrão).
  - [ ] Suporta múltiplos `Set-Cookie` (cada cookie vira um header separado).

---

## E04-T02 — Implementar `ResponseFactory::makeError` (com error_pages customizadas)

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`, `include/http/ResponseFactory.hpp`
- **Dependências:** E04-T01
- **Descrição:** verificar se `cfg.errorPages[code]` existe e o arquivo é legível → ler conteúdo e usar como body com `Content-Type: text/html`. Se não existir, gerar página HTML embutida genérica (`<html><body><h1>404 Not Found</h1></body></html>`).
- **Critérios de aceite:**
  - [ ] `errorPages[404] = "/errors/404.html"` faz a resposta 404 conter o conteúdo do arquivo.
  - [ ] Se o arquivo configurado não existir → fallback para página embutida (não lançar exceção).
  - [ ] `Content-Type: text/html; charset=utf-8` é setado.
  - [ ] Status reason ("Not Found", "Forbidden", etc.) bate com `HttpStatus::statusReason`.
  - [ ] Funciona para todos os códigos de erro suportados (400, 403, 404, 405, 408, 411, 413, 414, 500, 501, 505).

---

## E04-T03 — Implementar `ResponseFactory::makeRedirect`

- **Owner:** M3
- **Tamanho:** S
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** criar `Response` com `status_code = code` (default 302), `Location: url`, body vazio.
- **Critérios de aceite:**
  - [ ] `makeRedirect("https://example.com", 301)` produz `301 Moved Permanently` com `Location: https://example.com`.
  - [ ] Default `code = 302` quando omitido.
  - [ ] Body vazio (`Content-Length: 0`).
  - [ ] Apenas códigos 301 e 302 são aceitos; outros geram 500 ou erro.

---

## E04-T04 — Implementar `ResponseFactory::makeFile`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** abrir `fsPath` em modo binário, ler conteúdo completo para string, criar `Response(200)` com `Content-Type: mime`, `Content-Length`, body = conteúdo. Em erro de I/O, deixar para o chamador tratar (handler chamará `makeError(500)`).
- **Critérios de aceite:**
  - [ ] Arquivo binário (PNG, ICO) é lido sem corrupção (modo `ios::binary`).
  - [ ] Arquivo de 0 bytes gera `200 OK` com `Content-Length: 0`.
  - [ ] Arquivo grande (~10 MB) é lido em uma operação (aceitável para o subject).
  - [ ] Falha de leitura retorna `Response(500)` em vez de exceção (decisão do M3, mas documentar).

---

## E04-T05 — Implementar `ResponseFactory::makeAutoindex`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** abrir o diretório `fsPath` com `opendir()`, listar entradas com `readdir()`, gerar HTML simples com `<ul>` de `<a href>` apontando para `uriPath/<entry>`. Pular `.`. Mostrar `..` apenas se não for raiz.
- **Critérios de aceite:**
  - [ ] HTML válido — abre no Chrome sem erros de console.
  - [ ] Links são URLs relativas baseadas em `uriPath`, não caminhos do filesystem.
  - [ ] Diretórios aparecem com `/` no fim.
  - [ ] Ordenação alfabética (opcional mas recomendado para UX).
  - [ ] `Content-Type: text/html; charset=utf-8`.
  - [ ] Diretório vazio gera HTML válido com lista vazia (não erro).

---

## E04-T06 — Implementar `Router::route` (dispatch principal)

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Router.cpp`, `include/http/Router.hpp`
- **Dependências:** E04-T02, E04-T03, E02-T06, handlers do Épico 05
- **Descrição:** orquestrar o roteamento:
  1. `loc = vhost.findLocation(req.path())` — se `nullptr` → `makeError(404, vhost)`.
  2. Se `!methodAllowed(req.method(), *loc)` → `makeError(405, vhost)`.
  3. Se `loc->redirect` não vazio → `makeRedirect(loc->redirect, loc->redirectCode)`.
  4. Despachar para handler conforme método (`getH_`, `postH_`, `deleteH_`).
  5. Retornar a `Response`.
- **Critérios de aceite:**
  - [ ] `GET /` em config sem `/` location → 404.
  - [ ] `PUT /` em location com `methods GET POST DELETE` → 405 (com header `Allow: GET, POST, DELETE`).
  - [ ] `GET /old` com `redirect /new` → 302 com `Location: /new`.
  - [ ] Despacho correto para `GetHandler`, `PostHandler`, `DeleteHandler` por método.
  - [ ] Handler de método inválido (TRACE, OPTIONS) → 405 ou 501.

---

## E04-T07 — Implementar `Router::methodAllowed`

- **Owner:** M3
- **Tamanho:** S
- **Arquivos afetados:** `src/http/Router.cpp`
- **Dependências:** E04-T06
- **Descrição:** verificar se `method` está em `loc.methods`. Se `loc.methods` está vazio, considerar todos métodos permitidos (decisão de design — alternativamente, pode-se considerar como nenhum permitido; alinhar com docs).
- **Critérios de aceite:**
  - [ ] `methods = ["GET", "POST"]` aceita GET/POST e rejeita DELETE.
  - [ ] `methods = []` (não declarado) — comportamento documentado e consistente (recomendado: aceitar todos, igual ao Nginx).
  - [ ] Comparação case-sensitive (HTTP exige métodos maiúsculos).

---

## E04-T08 — Implementar `Request::cookie` (leitura de cookies)

- **Owner:** M3
- **Tamanho:** S
- **Arquivos afetados:** `src/http/Request.cpp`
- **Dependências:** E03-T03
- **Descrição:** parsear o header `Cookie: name1=val1; name2=val2` e retornar o valor da chave `name`. Se não existir, retornar string vazia.
- **Critérios de aceite:**
  - [ ] `Cookie: a=1; b=2` → `cookie("a") == "1"`, `cookie("b") == "2"`.
  - [ ] Espaços ao redor de `;` são ignorados.
  - [ ] Cookie não existente → `""`.
  - [ ] Múltiplos headers `Cookie` são tratados (concatenar valores).
  - [ ] Nomes case-sensitive (cookies são case-sensitive por RFC 6265).

---

## E04-T09 — Testes unitários de `ResponseFactory` e `Router`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `tests/unit/test_response_factory.cpp` (novo), `tests/scripts/test-router.sh`
- **Dependências:** E04-T01–E04-T08
- **Descrição:** suite de testes com `Request` mockado e `ServerConfig` inline. Verificar status codes, headers, e bodies de cada cenário (200, 301, 302, 400, 403, 404, 405, 413, autoindex, redirect, file).
- **Critérios de aceite:**
  - [ ] Pelo menos 12 casos de teste cobrindo todos os fatórios.
  - [ ] Mock `LocationConfig`/`ServerConfig` inline (sem precisar de `.conf`).
  - [ ] Saída textual é comparada com expected via `diff`.
  - [ ] Suite passa em `make test`.

---

## Resumo de tarefas

| ID | Tarefa | Tamanho | Dependências |
|----|--------|---------|-------------|
| E04-T01 | Response::toString | M | — |
| E04-T02 | makeError | M | T01 |
| E04-T03 | makeRedirect | S | T01 |
| E04-T04 | makeFile | M | T01 |
| E04-T05 | makeAutoindex | M | T01 |
| E04-T06 | Router::route | M | T02, T03, E02-T06, Épico 05 |
| E04-T07 | methodAllowed | S | T06 |
| E04-T08 | Request::cookie | S | E03-T03 |
| E04-T09 | Testes unitários | M | T01–T08 |
