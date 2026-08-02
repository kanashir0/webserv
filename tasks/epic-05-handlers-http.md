# Épico 05 — Handlers HTTP (GET / POST / DELETE)

> **Dono primário:** Membro 3 (M3)
> **Branch:** `feat/http-logic`
> **Valor entregue:** os 3 métodos HTTP exigidos pelo subject 42 funcionando — servir arquivos estáticos, fazer upload/CGI e deletar arquivos. É o que o avaliador da 42 testará primeiro.
> **Critério de "épico pronto":** servidor responde corretamente a `curl http://localhost:8080/`, `curl -F "file=@foo.txt" http://localhost:8080/upload`, `curl -X DELETE http://localhost:8080/upload/foo.txt` em ambiente integrado.

> **Status do épico (auditoria de 02/08/2026):** 🟡 **4 ✅ / 0 ⚠️ / 2 ❌** — GET, DELETE e o
> upload estão implementados e corretos. Mas **o épico não está pronto**: o CGI é um stub
> (T03) e um GET a script `.py` devolve o código-fonte
> ([BUG-05-01](#bug-05-01--get-em-script-cgi-devolve-o-código-fonte), crítico). O upload
> também não funciona no repositório limpo por falta do diretório `www/uploads`
> ([BUG-05-02](#bug-05-02--wwwuploads-não-existe-no-repositório)).
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

---

## ✅ E05-T01 — Implementar `GetHandler::handle`, `serveFile`, `serveDirectory`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA — cumpre todos os critérios escritos; a lacuna de CGI é escopo novo, ver [BUG-05-01](#bug-05-01--get-em-script-cgi-devolve-o-código-fonte)
- **Tamanho:** L
- **Arquivos afetados:** `src/http/handlers/GetHandler.cpp`, `include/http/handlers/GetHandler.hpp`
- **Dependências:** E04-T02, E04-T04, E04-T05
- **Descrição:** orquestrar a entrega de recursos estáticos:
  - `fsPath` resolvido por `PathResolver::resolve` (decode + normalização + strip do prefixo da location + join com o root).
  - `stat()` no path: se `S_ISREG` → `serveFile`; se `S_ISDIR` → `serveDirectory`; senão → 403.
  - `serveDirectory`: tenta `loc.index` (com fallback para `srv.index`) → se existe e é arquivo → `serveFile`. Senão se `loc.autoindex` → `makeAutoindex`. Senão → 403.
- **Critérios de aceite:**
  - [x] `GET /` com `loc.root=./www` e `index index.html` serve `./www/index.html`. ← verificado no smoke test
  - [x] `GET /img/logo.png` serve arquivo binário sem corrupção.
  - [x] Diretório sem index e `autoindex off` → 403.
  - [x] Diretório sem index e `autoindex on` → HTML de listagem.
  - [x] Path traversal (`/../etc/passwd`) é bloqueado antes do `stat()`. ← verificado: 403
  - [x] MIME type correto via `MimeTypes::fromPath()`.
  - [x] Diretório acessado sem barra final redireciona com 301 para o path com `/` (comportamento Nginx, garante que os links relativos do autoindex funcionem).

---

## ✅ E05-T02 — Implementar `PostHandler::handle` + `handleUpload`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA — depende de [BUG-05-02](#bug-05-02--wwwuploads-não-existe-no-repositório) para funcionar num clone limpo
- **Tamanho:** L
- **Arquivos afetados:** `src/http/handlers/PostHandler.cpp`, `include/http/handlers/PostHandler.hpp`
- **Dependências:** E04-T02, E03-T04
- **Descrição:** se a URI bate com extensão em `loc.cgi` → `handleCgi` (E05-T03). Senão → `handleUpload`. Upload extrai filename do header `Content-Disposition` (multipart/form-data) ou da URI, e escreve `req.body()` em `loc.uploadStore + filename`. Resposta `201 Created` com `Content-Location`.
- **Critérios de aceite:**
  - [x] Upload simples com `application/octet-stream` salva o body inteiro como arquivo.
  - [x] Upload `multipart/form-data` extrai o primeiro campo do form, incluindo o `filename=` do `Content-Disposition` e o boundary (com ou sem aspas).
  - [x] Body > `clientMaxBodySize` já foi rejeitado pelo parser (413) — handler não vê. ← verificado
  - [x] `loc.uploadStore` vazio, inexistente ou sem permissão de escrita → 500 com log.
  - [x] Filename com `..`, `/` ou `\` é sanitizado (`basenameOf` + rejeição de caracteres de controle); sem nome utilizável, gera um nome próprio.
  - [x] Response 201 com `Content-Location: /upload/<filename>` percent-encoded.

---

## ❌ E05-T03 — Implementar `PostHandler::handleCgi` (dispatch para CGI)

- **Owner:** M3 (com integração do Membro 1 no Épico 06)
- **Status:** ❌ PENDENTE — stub retornando 501 (`src/http/handlers/PostHandler.cpp:211-217`). **Escopo alterado em 02/08/2026:** a detecção de CGI sai daqui e sobe para o `Router` — ver [E06-T09](epic-06-cgi.md#-e06-t09--detectar-cgi-no-routerroute-get-e-post)
- **Tamanho:** M
- **Arquivos afetados:** `src/http/handlers/PostHandler.cpp`
- **Dependências:** E06-T03, E06-T04, E06-T06, E06-T07, E06-T09
- **Descrição:** com a detecção movida para o `Router` (E06-T09), o que sobra aqui é o
  caminho **POST** da execução: montar o `CgiEnv` com o body da request, instanciar o
  `CgiHandler`, registrá-lo no `EventLoop` e devolver o controle ao `Client`, que aguarda em
  `WAITING_CGI`. A entrega do body pelo `stdin` do script é o que distingue este caminho do
  GET.
- **Critérios de aceite:**
  - [ ] `POST /cgi-bin/post_echo.py` com body retorna o body ecoado pelo script.
  - [ ] `CONTENT_LENGTH` e `CONTENT_TYPE` chegam ao script (E06-T01).
  - [ ] Interpretador inválido (path inexistente) → 500, sem processo zumbi.
  - [ ] Headers do CGI (`Status:`, `Content-Type:`) são respeitados na resposta final (E06-T06).
  - [ ] O `Client` aguarda no estado `WAITING_CGI` até `cgi.finished()`, sem bloquear o loop.

---

## ✅ E05-T04 — Implementar `DeleteHandler::handle`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/handlers/DeleteHandler.cpp`, `include/http/handlers/DeleteHandler.hpp`
- **Dependências:** E04-T02
- **Descrição:** resolver o `fsPath` e remover o arquivo. `stat()`: se não existe → 404; se não é arquivo regular → 403; se é arquivo → `std::remove()`. Sucesso → `Response(204)` sem body.
- **Critérios de aceite:**
  - [x] `DELETE /upload/foo.txt` em arquivo existente retorna 204 e remove o arquivo do disco.
  - [x] `DELETE /naoexiste` → 404.
  - [x] `DELETE /upload` (diretório) → 403.
  - [x] Permission denied no `unlink` → 403 (checado via `access(parentDir, W_OK|X_OK)` **antes** da remoção, já que a permissão de apagar é do diretório, não do arquivo).
  - [x] Path traversal bloqueado.

> **Detalhe de simetria:** quando a location tem `upload_store`, o DELETE resolve **dentro
> do `upload_store`**, não do `root` — senão a location de upload seria assimétrica (o POST
> grava num lugar e o DELETE procura em outro). Está comentado no código.

---

## ✅ E05-T05 — Validações de permissão e segurança

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA — implementada como `src/http/PathResolver.cpp` (arquivo novo, não previsto na tarefa)
- **Tamanho:** S
- **Arquivos afetados:** `src/http/PathResolver.cpp`, `include/http/PathResolver.hpp`, todos os handlers
- **Dependências:** E05-T01, E05-T02, E05-T04
- **Descrição:** centralizar as funções de sanitização de path e validação de permissão, para que GET, POST e DELETE compartilhem exatamente a mesma lógica em vez de cada handler reinventar a sua.
- **Critérios de aceite:**
  - [x] Função utilitária central: `PathResolver::resolve(rawPath, loc, srv, fsPath)`, usada por todos os handlers.
  - [x] Normalização própria rejeita path que sai do root — `normalizePath()` conta os `..` sobre uma pilha de segmentos e falha se ela esvaziar, o que bloqueia o traversal **antes** de tocar o filesystem (mais robusto que `realpath()`, que exige o arquivo existir).
  - [x] Null byte (`%00`) na URI é rejeitado com 400 — `percentDecode` recusa `%00` explicitamente.
  - [x] Percent-encoding inválido (`%ZZ`, `%A` truncado) → 400.
  - [x] `curl --path-as-is http://localhost:8080/../../etc/passwd` retorna 403, nunca 200. ← verificado no smoke test
  - [x] Permissão de I/O validada via `access()` antes de servir/apagar (`R_OK` em arquivos, `R_OK|X_OK` em diretórios, `W_OK|X_OK` no diretório pai para o DELETE).

---

## ❌ E05-T06 — Cobertura dos handlers no `curl-suite.sh`

- **Owner:** M3
- **Status:** ❌ PENDENTE — **reescopada em 02/08/2026** (era: "Testes unitários e de integração dos handlers" em `tests/unit/test_handlers.cpp`)
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/curl-suite.sh`
- **Dependências:** E05-T01–E05-T05
- **Motivo do reescopo:** o padrão de teste do projeto é `curl-suite.sh` + `test-edge-cases.sh` (ver a política em [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md)). Sem testes unitários em C++. Nos handlers a perda é pequena: eles são justamente a camada que o `curl` exercita bem.
- **Descrição:** cobrir os três métodos ponta a ponta, incluindo o ciclo completo
  POST → GET → DELETE sobre o mesmo arquivo, que valida a simetria `upload_store`.
- **Critérios de aceite:**
  - [ ] **Setup/teardown:** o script cria `www/uploads` se faltar e limpa o conteúdo antes e depois, garantindo idempotência (cross-ref [BUG-05-02](#bug-05-02--wwwuploads-não-existe-no-repositório)).
  - [ ] GET: arquivo (200), diretório com index (200), diretório com autoindex (200 + `<title>Index of`), diretório sem index e sem autoindex (403), inexistente (404).
  - [ ] POST: `--data-binary` simples → 201 + header `Content-Location`; `-F "file=@..."` multipart → 201 e o arquivo aparece em `www/uploads` com o nome do form.
  - [ ] Ciclo completo: POST cria → GET no `Content-Location` devolve 200 com o mesmo conteúdo → DELETE devolve 204 → GET seguinte devolve 404.
  - [ ] DELETE: arquivo (204), diretório (403), inexistente (404).
  - [ ] Path traversal nos três métodos com `--path-as-is`: nunca 200.
  - [ ] Byte-a-byte: `cmp` entre o arquivo enviado e o recebido num round-trip binário (PNG).

---

## Resumo de tarefas

| ID | Tarefa | Status | Tamanho | Dependências |
|----|--------|--------|---------|-------------|
| E05-T01 | GetHandler | ✅ | L | E04-T02, T04, T05 |
| E05-T02 | PostHandler::handleUpload | ✅ | L | E04-T02, E03-T04 |
| E05-T03 | PostHandler::handleCgi | ❌ | M | Épico 06 |
| E05-T04 | DeleteHandler | ✅ | M | E04-T02 |
| E05-T05 | Sanitização de path (PathResolver) | ✅ | S | T01, T02, T04 |
| E05-T06 | Cobertura no curl-suite.sh | ❌ reescopada | M | T01–T05 |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`.

### BUG-05-01 — `GET` em script CGI devolve o código-fonte

- **Origem:** E05-T01 (sintoma) — a correção é a nova
  [E06-T09](epic-06-cgi.md#-e06-t09--detectar-cgi-no-routerroute-get-e-post)
- **Onde:** `src/http/handlers/GetHandler.cpp:17-40` (não consulta `loc.cgi`);
  `src/http/handlers/PostHandler.cpp:147-150` (a detecção de CGI vive só aqui)
- **Sintoma:** verificado com o servidor rodando —

  ```
  $ curl -s http://localhost:8080/cgi-bin/hello.py
  #!/usr/bin/env python3
  import sys
  sys.stdout.write("Content-Type: text/plain\r\n\r\n")
  ...
  ```

  O `GetHandler` trata `.py` como arquivo estático qualquer e serve o conteúdo. Como a
  detecção de CGI foi implementada dentro do `PostHandler`, ela simplesmente não existe
  para o método GET.
- **Esperado:** a detecção sobe para o `Router::route`, antes do dispatch por método, e vale
  para GET e POST — é onde o `LocationConfig` já está em mãos e onde todos os métodos
  passam. Escopo que nenhuma tarefa previa (o E05 só falava de CGI no POST), por isso virou
  tarefa nova em vez de um bug de implementação.
- **Severidade:** **Crítica** — divulgação de código-fonte. Um script que leia credenciais
  ou toque em `upload_store` entrega tudo. É o tipo de coisa que reprova no defense, e o
  critério de "épico pronto" do E06 já exigia GET a scripts Python.

### BUG-05-02 — `www/uploads` não existe no repositório

- **Origem:** E05-T02 (o handler está certo; o repositório é que está incompleto)
- **Onde:** `conf/default.conf:19` aponta `upload_store ./www/uploads`; o diretório não está
  versionado (o Git não guarda diretórios vazios).
- **Sintoma:** num clone limpo, **todo POST de upload retorna 500**. Verificado no smoke
  test:

  ```
  $ curl -o /dev/null -w "%{http_code}" --data-binary "abc" http://localhost:8080/upload/a.txt
  500
  [ERROR] PostHandler: upload_store inacessivel: "./www/uploads"
  ```

  O handler faz a coisa certa (`stat` + `S_ISDIR` + `access(W_OK|X_OK)` → 500 com log), mas
  quem clona o repositório e roda o `curl-suite` vai achar que o upload está quebrado.
- **Esperado:** versionar `www/uploads/.gitkeep`. O `curl-suite.sh` também deve criar e
  limpar o diretório no setup (E05-T06 e E08-T01), para não depender do estado do
  filesystem. E [BUG-02-01](epic-02-parser-configuracao.md#bug-02-01--três-validações-semânticas-de-e02-t05-não-foram-implementadas)
  faria isso falhar no startup em vez de num 500 obscuro.
- **Severidade:** Média — não é bug de código, mas custa uma sessão de debug a quem
  esbarrar nele.
