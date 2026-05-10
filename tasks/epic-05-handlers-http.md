# Épico 05 — Handlers HTTP (GET / POST / DELETE)

> **Dono primário:** Membro 3 (M3)
> **Branch:** `feat/http-logic`
> **Valor entregue:** os 3 métodos HTTP exigidos pelo subject 42 funcionando — servir arquivos estáticos, fazer upload/CGI e deletar arquivos. É o que o avaliador da 42 testará primeiro.
> **Critério de "épico pronto":** servidor responde corretamente a `curl http://localhost:8080/`, `curl -F "file=@foo.txt" http://localhost:8080/upload`, `curl -X DELETE http://localhost:8080/upload/foo.txt` em ambiente integrado.

---

## E05-T01 — Implementar `GetHandler::handle`, `serveFile`, `serveDirectory`

- **Owner:** M3
- **Tamanho:** L
- **Arquivos afetados:** `src/http/handlers/GetHandler.cpp`, `include/http/handlers/GetHandler.hpp`
- **Dependências:** E04-T02, E04-T04, E04-T05
- **Descrição:** orquestrar a entrega de recursos estáticos:
  - `fsPath = loc.root + req.path()` (com cuidado de não duplicar `/`).
  - `stat()` no path: se `S_ISREG` → `serveFile`; se `S_ISDIR` → `serveDirectory`; senão → 404.
  - `serveFile`: verifica permissão de leitura (`access(R_OK)`) → `makeFile()`. Em ENOENT → 404; EACCES → 403.
  - `serveDirectory`: tenta `loc.index` → se existe e é arquivo → `serveFile`. Senão se `loc.autoindex == true` → `makeAutoindex`. Senão → 403.
- **Critérios de aceite:**
  - [ ] `GET /` com `loc.root=./www` e `loc.index=index.html` serve `./www/index.html`.
  - [ ] `GET /img/logo.png` serve arquivo binário sem corrupção.
  - [ ] Diretório sem index e `autoindex off` → 403.
  - [ ] Diretório sem index e `autoindex on` → HTML de listagem.
  - [ ] Path traversal (`/../etc/passwd`) é bloqueado — sanitização do `path` antes do `stat()`.
  - [ ] MIME type correto via `mime::fromPath()`.

---

## E05-T02 — Implementar `PostHandler::handle` + `handleUpload`

- **Owner:** M3
- **Tamanho:** L
- **Arquivos afetados:** `src/http/handlers/PostHandler.cpp`, `include/http/handlers/PostHandler.hpp`
- **Dependências:** E04-T02, E03-T04
- **Descrição:** se a URI bate com extensão em `loc.cgi` → `handleCgi` (E05-T03). Senão → `handleUpload`. Upload extrai filename do header `Content-Disposition` (multipart/form-data) ou da URI, e escreve `req.body()` em `loc.uploadStore + filename`. Resposta `201 Created` com `Content-Location` apontando para a URL do arquivo.
- **Critérios de aceite:**
  - [ ] Upload simples com `application/octet-stream` salva o body inteiro como arquivo.
  - [ ] Upload `multipart/form-data` extrai pelo menos o primeiro campo do form.
  - [ ] Body > `clientMaxBodySize` já foi rejeitado pelo parser (413) — handler não vê.
  - [ ] `loc.uploadStore` vazio ou inválido → 500.
  - [ ] Filename com `..`, `/` ou `\` é sanitizado/rejeitado.
  - [ ] Response 201 com `Content-Location: /upload/<filename>`.

---

## E05-T03 — Implementar `PostHandler::handleCgi` (dispatch para CGI)

- **Owner:** M3 (com integração do Membro 1 no Épico 06)
- **Tamanho:** M
- **Arquivos afetados:** `src/http/handlers/PostHandler.cpp`
- **Dependências:** E06-T01–E06-T07
- **Descrição:** identificar interpretador via `loc.cgi[ext]`, instanciar `CgiHandler`, registrar no `EventLoop` e aguardar `finished()`. Quando terminar, chamar `takeResponse()`. Para a versão síncrona inicial (stub), pode invocar diretamente; a versão assíncrona é integração do Épico 06.
- **Critérios de aceite:**
  - [ ] `POST /cgi-bin/post_echo.py` com body retorna o body ecoado pelo script.
  - [ ] Interpretador inválido (path inexistente) → 500.
  - [ ] Headers do CGI (`Status:`, `Content-Type:`) são respeitados na resposta final.
  - [ ] Em modo assíncrono (Épico 06), o `Client` aguarda no estado `ROUTING` até `cgi.finished()`.

---

## E05-T04 — Implementar `DeleteHandler::handle`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/handlers/DeleteHandler.cpp`, `include/http/handlers/DeleteHandler.hpp`
- **Dependências:** E04-T02
- **Descrição:** `fsPath = loc.root + req.path()`. `stat()`: se não existe → 404; se é diretório → 403; se é arquivo → `unlink()`. Sucesso → `Response(204)` (sem body). Falha em `unlink` (permissão) → 403 ou 500.
- **Critérios de aceite:**
  - [ ] `DELETE /upload/foo.txt` em arquivo existente retorna 204 e remove o arquivo do disco.
  - [ ] `DELETE /naoexiste` → 404.
  - [ ] `DELETE /upload` (diretório) → 403.
  - [ ] Permission denied no `unlink` → 403.
  - [ ] Path traversal bloqueado.

---

## E05-T05 — Validações de permissão e segurança

- **Owner:** M3
- **Tamanho:** S
- **Arquivos afetados:** todos os handlers
- **Dependências:** E05-T01, E05-T02, E05-T04
- **Descrição:** centralizar funções utilitárias para: (1) sanitizar path contra `..`, símbolos especiais, null bytes; (2) validar permissão de I/O via `access()`; (3) garantir que o `fsPath` resolvido está dentro de `loc.root` (proteção real contra path traversal).
- **Critérios de aceite:**
  - [ ] Função utilitária `safePath(root, path)` retorna path absoluto canônico ou erro.
  - [ ] `realpath()` ou implementação manual rejeita path que sai de `root`.
  - [ ] Null byte (`%00`) na URI é rejeitado com 400.
  - [ ] Testes manuais com `curl http://localhost:8080/../../etc/passwd` retornam 400 ou 403, nunca 200.

---

## E05-T06 — Testes unitários e de integração dos handlers

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `tests/unit/test_handlers.cpp` (novo), `tests/scripts/curl-suite.sh`
- **Dependências:** E05-T01–E05-T05
- **Descrição:** testes com `Request` mockado e diretório temporário. Verificar:
  - GET de arquivo, diretório com index, diretório com autoindex, 404.
  - POST upload de arquivo, POST com body grande (verifica que não chegou).
  - DELETE de arquivo, DELETE de diretório, DELETE inexistente.
  - Path traversal em todos os métodos.
- **Critérios de aceite:**
  - [ ] Pelo menos 15 casos de teste cobrindo cenários positivos e negativos.
  - [ ] Setup/teardown criam e limpam diretório temporário.
  - [ ] `curl-suite.sh` é estendido com casos novos para testes ponta-a-ponta.
  - [ ] Sem warnings com `-Werror`.

---

## Resumo de tarefas

| ID | Tarefa | Tamanho | Dependências |
|----|--------|---------|-------------|
| E05-T01 | GetHandler | L | E04-T02, T04, T05 |
| E05-T02 | PostHandler::handleUpload | L | E04-T02, E03-T04 |
| E05-T03 | PostHandler::handleCgi | M | Épico 06 |
| E05-T04 | DeleteHandler | M | E04-T02 |
| E05-T05 | Sanitização de path | S | T01, T02, T04 |
| E05-T06 | Testes | M | T01–T05 |
