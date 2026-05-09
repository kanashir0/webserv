---
name: webserv-http
description: Apoio ao Membro 3 (branch feat/http-logic) na implementação de Router, handlers GET/POST/DELETE, ResponseFactory, MimeTypes e session/. Use ao escrever ou revisar código em src/http/ (exceto RequestParser) ou src/session/.
---

# webserv-http — guia do Membro 3

Você é o "cérebro": dado um `Request` validado e o `ServerConfig`/`LocationConfig`
correspondente, decide o que retornar.

## Princípios

1. **Handlers não fazem I/O de socket.** Eles montam um `Response` que o
   M1 envia depois. Leitura de arquivo do disco é OK (subject permite — só
   sockets/pipes precisam de poll).
2. **`Response::setBody()` atualiza `Content-Length` automaticamente.**
   Nunca seta o header manualmente após `setBody()`.
3. **`Router::route(req, vhost)` retorna sempre um `Response`** — nunca lança
   exceção pela borda; converta erros internos em 500 via `ResponseFactory`.
4. **Strategy + Factory** são os padrões centrais aqui.

## Router — longest-prefix match

```cpp
const LocationConfig* Router::findLocation(const std::string& uri,
                                           const ServerConfig& vhost) const;
```

Ordene as locations por tamanho do prefix (decrescente) e devolva a primeira
que casar. Comportamento Nginx — `/upload` ganha de `/` para uma URI
`/upload/foo.txt`.

Vhost: M1 entrega o `ServerConfig` certo baseado em `Host:` + porta.
Se `Host` ausente em HTTP/1.1: 400 Bad Request.

## IMethodHandler

```cpp
class IMethodHandler {
public:
    virtual ~IMethodHandler() {}
    virtual Response handle(const Request&, const LocationConfig&) const = 0;
};
```

Implementações: `GetHandler`, `PostHandler`, `DeleteHandler`.

### Validação de método

Antes de despachar, verifique a whitelist `methods` do `LocationConfig`.
Método não permitido → **405 Method Not Allowed** com header `Allow:` listando
os permitidos (RFC 7231 §6.5.5).

## GetHandler

Fluxo:
1. Resolva path absoluto: `location.root` + (URI sem o prefix da location).
2. **Path traversal:** rejeite `..` (canonicalize com `realpath` ou bloqueie
   strings `/../` na URI antes). Vazamento aqui é vulnerabilidade séria.
3. Se path é diretório:
   - Tente cada `index` da config; se achar, sirva esse arquivo.
   - Senão, se `autoindex on` → `ResponseFactory::makeAutoindex(dir)`.
   - Senão → 403 Forbidden.
4. Se path é arquivo: 200 + `Content-Type` via `MimeTypes::lookup(ext)` +
   body = conteúdo.
5. `stat()` falha com ENOENT → 404; EACCES → 403.

## PostHandler

- Para `/upload` (location com `upload_store`): salve `req.body()` em
  `upload_store/<filename>`. Filename: do `Content-Disposition` (multipart),
  ou gere UUID/timestamp. Retorne 201 Created com `Location:` header.
- Para CGI: delegue ao `CgiHandler` (a integração precisa do M1 — assíncrona).
- Sem destino válido: 405.

## DeleteHandler

- `unlink()` no path resolvido. Sucesso → 204 No Content.
- ENOENT → 404. EACCES → 403. EISDIR → 403 (não deletar diretório).

## ResponseFactory

```cpp
static Response makeError(int code, const ServerConfig& cfg);
static Response makeFile(const std::string& path);
static Response makeAutoindex(const std::string& dir, const std::string& uri);
static Response makeRedirect(int code, const std::string& location);
static Response makeFromCgi(const std::string& cgiOutput);
```

`makeError` consulta `error_page` da config; se não tem custom, gera HTML
mínimo embutido (`<html><body><h1>404 Not Found</h1></body></html>`).

`makeFromCgi` parseia headers do CGI (linha em branco separa headers do body),
extrai `Status:` (default 200), preserva `Content-Type`.

## MimeTypes

Tabela mínima exigida: `.html`, `.htm`, `.css`, `.js`, `.png`, `.jpg`, `.jpeg`,
`.gif`, `.ico`, `.txt`, `.json`, `.pdf`. Default: `application/octet-stream`.

## Sessions (bônus)

`SessionStore::get(id)` cria se não existir. `SessionStore::gc()` é chamado a
cada tick do `EventLoop` — remova sessões com `lastAccess + ttl < now()`.
Cookie: `Set-Cookie: SID=<uuid>; Path=/; HttpOnly`.

## Checklist de PR (M3)

- [ ] Path traversal bloqueado (teste com `curl http://localhost:8080/../etc/passwd`)
- [ ] 405 retorna header `Allow:`
- [ ] Autoindex gera HTML válido com links clicáveis
- [ ] `Content-Type` correto em pelo menos html/css/js/png
- [ ] `error_page` da config sobrescreve as páginas embutidas
- [ ] Edge cases 1, 2, 3 do `webserv-test` passam
- [ ] Handlers não chamam `recv`/`send` (só leem disco)
