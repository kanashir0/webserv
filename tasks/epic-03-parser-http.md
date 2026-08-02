# Épico 03 — Parser HTTP de Requisições

> **Dono primário:** Membro 2 (M2)
> **Branch:** `feat/parsers`
> **Valor entregue:** transformação de bytes brutos do socket em objeto `Request` validado e imutável. É o contrato entre o motor de rede (M1, que fornece bytes) e a lógica HTTP (M3, que consome `Request`). Sem este épico, o servidor não entende HTTP.
> **Critério de "épico pronto":** parser aceita requisições HTTP/1.0 e HTTP/1.1 válidas (incluindo `Transfer-Encoding: chunked`), rejeita malformadas com o status correto (400/411/413/414/505), e suporta envio fragmentado (recv parcial).

> **Status do épico (auditoria de 02/08/2026):** 🟢 **9 ✅ / 0 ⚠️ / 1 ❌** — **o módulo mais
> sólido do projeto.** Todas as tarefas de implementação estão fechadas e o parser entrega
> mais do que foi pedido (ver a nota abaixo). Só falta a cobertura de testes (reescopada).
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

> **Entregue além do escopo:** o parser implementa duas defesas contra **HTTP request
> smuggling** que nenhuma tarefa pediu — rejeita `Content-Length` duplicado
> (`RequestParser.cpp:188-193`) e rejeita `Content-Length` + `Transfer-Encoding` na mesma
> request (`:206-209`). Também exige o header `Host` em HTTP/1.1 conforme RFC 7230 §5.4
> (`:198-201`) e trata chunk extensions (`;` na size-line). Vale citar no defense.

---

## ✅ E03-T01 — Implementar `RequestParser::feed` (entrada de dados)

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`, `include/http/RequestParser.hpp`
- **Dependências:** nenhuma (módulo independente; usa apenas `common/`)
- **Descrição:** appendar `data[0..n]` no buffer interno `buf_`. Dependendo de `state_`, despachar para o método de parsing correspondente (`parseRequestLine`, `parseHeaders`, `parseBodyByLength`, `parseBodyChunked`). Retornar `FeedResult::NEED_MORE`, `COMPLETE`, ou um dos códigos de erro.
- **Critérios de aceite:**
  - [x] Aceita `data` em qualquer granularidade (1 byte, 100 bytes, request inteira).
  - [x] `state_` avança apenas quando há bytes suficientes; senão retorna `NEED_MORE`.
  - [x] `maxBody` é validado durante o parsing — não acumula body acima do limite antes de rejeitar.
  - [x] Buffer não cresce indefinidamente — bytes consumidos são removidos via `buf_.erase(0, n)`, e um backstop de 16 KB (`kMaxPreBodyBytes`) protege a fase de headers.

> **Detalhe de design:** `feed(NULL, 0, maxBody)` processa o que já está no buffer sem
> acrescentar bytes. É exatamente o que o `Client` precisa para consumir uma request em
> pipelining após o `take()` — e é o que resolve
> [BUG-01-09](epic-01-motor-de-rede.md#bug-01-09--keep-alive-descarta-requests-em-pipelining).

---

## ✅ E03-T02 — Implementar `parseRequestLine`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T01
- **Descrição:** procurar `\r\n` no `buf_`. A linha antes deve ter formato `METHOD URI VERSION`. Validar: método é token ASCII (sem espaços), URI começa com `/` e tem ≤ 8192 chars, versão é `HTTP/1.0` ou `HTTP/1.1`. Preencher `building_.method_`, `uri_`, `version_`.
- **Critérios de aceite:**
  - [x] `GET / HTTP/1.1\r\n` é parseado com sucesso.
  - [x] URI > 8192 chars retorna `URI_TOO_LONG` (414) — inclusive antes da linha fechar, via backstop `kMaxUriLength + 64`.
  - [x] Método com espaço ou caractere inválido retorna `BAD_REQUEST` (400) — `isToken()` segue a definição de tchar da RFC 7230 §3.2.6.
  - [x] `HTTP/2.0` retorna `HTTP_VERSION_UNSUPPORTED` (505). ← verificado no smoke test
  - [x] Linha sem `\r\n` ainda no buffer → `NEED_MORE`.
  - [x] Suporta apenas `\r\n` como separador (não aceita só `\n`).

---

## ✅ E03-T03 — Implementar `parseHeaders`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T02
- **Descrição:** ler linhas `Key: Value\r\n` até encontrar a linha vazia `\r\n`. Inserir em `building_.headers_` (que usa `CaseInsensitiveLess`). Ao encontrar a linha vazia, analisar `Content-Length` e `Transfer-Encoding` para decidir próximo estado: `BODY_LENGTH`, `BODY_CHUNKED` ou `DONE` (sem body).
- **Critérios de aceite:**
  - [x] `Content-Length: 42` → `state_ = BODY_LENGTH`, `contentLength_ = 42`.
  - [x] `Transfer-Encoding: chunked` → `state_ = BODY_CHUNKED` (e qualquer outro valor de TE → 400).
  - [x] Header malformado (sem `:` ou com nome vazio) → `BAD_REQUEST`.
  - [x] Espaços ao redor do valor são removidos via `StringUtils::trim`.
  - [x] Headers duplicados: o último vence — **exceto `Content-Length`**, que é rejeitado se duplicado (defesa contra request smuggling).
  - [x] Total de headers > 100 ou tamanho > 8 KB → `BAD_REQUEST`.
  - [x] HTTP/1.1 sem header `Host` → `BAD_REQUEST` (RFC 7230 §5.4). ← verificado no smoke test

---

## ✅ E03-T04 — Implementar `parseBodyByLength` + validação `maxBody`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T03
- **Descrição:** ler exatamente `contentLength_` bytes do buffer e copiar para `building_.body_`. Se `contentLength_ > maxBody` → `BODY_TOO_LARGE`. Se POST sem `Content-Length` e sem `Transfer-Encoding` → `LENGTH_REQUIRED` (411).
- **Critérios de aceite:**
  - [x] `Content-Length: 0` é válido (body vazio) — vai direto para `DONE`.
  - [x] Body parcial (chegaram só 5 de 42 bytes) → `NEED_MORE`.
  - [x] `Content-Length` excede `maxBody` → `BODY_TOO_LARGE` **antes** de receber o body inteiro. ← verificado: 2 MB contra limite de 1m devolveu 413
  - [x] POST sem `Content-Length` nem `Transfer-Encoding` → `errorStatus = 411`.
  - [x] GET com body é aceito; o body é ignorado pelos handlers.

---

## ✅ E03-T05 — Implementar `parseBodyChunked` (Transfer-Encoding: chunked)

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** L
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T03
- **Descrição:** implementar protocolo chunked: cada chunk começa com tamanho em hex `\r\n`, depois `<size>` bytes, depois `\r\n`. Chunk de tamanho 0 (`0\r\n\r\n`) finaliza. Acumular total em `building_.body_` validando contra `maxBody`.
- **Critérios de aceite:**
  - [x] Body chunked simples (3 chunks + terminator) é decodificado corretamente.
  - [x] Tamanho em hex maiúsculo e minúsculo funciona — `parseChunkSize` próprio, com teto de 8 dígitos contra overflow (mais seguro que `strtol`, que não detecta overflow bem).
  - [x] Chunk com tamanho inválido (não-hex) → `BAD_REQUEST`.
  - [x] Soma dos chunks excede `maxBody` → `BODY_TOO_LARGE` a meio do parsing.
  - [x] Trailers chunked são lidos e ignorados, com teto de 8 KB.
  - [x] Recebimento parcial em qualquer ponto do chunked retorna `NEED_MORE` — só consome o chunk quando size-line + dados + CRLF estão todos no buffer.
  - [x] Chunk extensions (`1a;foo=bar\r\n`) são toleradas e descartadas.

---

## ✅ E03-T06 — Implementar `splitUri` (separação de path e query)

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T02
- **Descrição:** após parsear `uri_`, encontrar primeiro `?` e dividir em `path_` e `query_`. URIs sem `?` têm `query_ = ""`.
- **Critérios de aceite:**
  - [x] `/upload/foo.png?thumb=1` → `path_="/upload/foo.png"`, `query_="thumb=1"`.
  - [x] `/sem-query` → `path_="/sem-query"`, `query_=""`.
  - [x] `/?` → `path_="/"`, `query_=""`.
  - [x] Múltiplos `?` → divide no primeiro.
  - [x] Path com `..` é mantido como está — a sanitização é do `PathResolver` (M3, E05-T05).

---

## ✅ E03-T07 — Implementar `Request::keepAlive`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/http/Request.cpp`
- **Dependências:** E03-T03
- **Descrição:** ler header `Connection`. Em HTTP/1.1, default = keep-alive a menos que `Connection: close`. Em HTTP/1.0, default = close a menos que `Connection: keep-alive`. Comparação case-insensitive.
- **Critérios de aceite:**
  - [x] `HTTP/1.1` sem header `Connection` → `keepAlive() == true`. ← verificado: 2 requests na mesma conexão
  - [x] `HTTP/1.1` com `Connection: close` → `false`.
  - [x] `HTTP/1.0` sem header → `false`.
  - [x] `HTTP/1.0` com `Connection: keep-alive` → `true`.
  - [x] Funciona com qualquer capitalização (via `StringUtils::iequals`).

---

## ✅ E03-T08 — Implementar `RequestParser::take` e `reset`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA — a implementação está correta, mas o `Client` anula o benefício; ver [BUG-01-09](epic-01-motor-de-rede.md#bug-01-09--keep-alive-descarta-requests-em-pipelining)
- **Tamanho:** S
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T01
- **Descrição:** `take()` retorna o `Request` construído (`building_`) e reinicia o parser internamente. `reset()` limpa todos os estados (buffer, contadores, building_) para reutilização da mesma instância em conexões keep-alive.
- **Critérios de aceite:**
  - [x] Após `take()`, `state_ == METHOD` e os contadores estão zerados.
  - [x] Bytes residuais no buffer (próxima request em pipelining) são **preservados** em `take()`.
  - [x] `reset()` chamado a qualquer momento volta o parser ao estado inicial limpo (inclusive descartando `buf_`).

> **Atenção de integração:** a distinção entre `take()` (preserva `buf_`) e `reset()`
> (descarta `buf_`) é deliberada e correta. O `Client::onWritable` usa `reset()` no caminho
> de keep-alive, que é o caso em que ele deveria usar `take()` + `feed(NULL, 0, ...)`.

---

## ✅ E03-T09 — Tratamento de erros e `errorStatus`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T01–E03-T05
- **Descrição:** ao retornar qualquer `FeedResult` de erro, setar `errorStatus_` com o código HTTP correspondente. `errorStatus()` retorna o valor para o `Client::onReadable()` construir a resposta.
- **Critérios de aceite:**
  - [x] `BAD_REQUEST` → `errorStatus = 400`. ← verificado
  - [x] `URI_TOO_LONG` → `errorStatus = 414`.
  - [x] `BODY_TOO_LARGE` → `errorStatus = 413`. ← verificado
  - [x] `HTTP_VERSION_UNSUPPORTED` → `errorStatus = 505`. ← verificado
  - [x] POST sem `Content-Length` → `errorStatus = 411`.
  - [x] Um parser em `ERROR` continua devolvendo o mesmo erro em `feed()` subsequentes (não corrompe estado).

---

## ❌ E03-T10 — Cobertura do `RequestParser` no `test-edge-cases.sh`

- **Owner:** M2
- **Status:** ❌ PENDENTE — **reescopada em 02/08/2026** (era: "Testes unitários do `RequestParser`" em `tests/unit/test_request_parser.cpp`)
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/test-edge-cases.sh`
- **Dependências:** E03-T01–E03-T09
- **Motivo do reescopo:** o padrão de teste do projeto é `curl-suite.sh` + `test-edge-cases.sh` (ver a política em [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md)). Sem testes unitários em C++.
- **Descrição:** exercitar o parser pelo socket, com `printf | nc` para os casos que o `curl` não consegue produzir (request malformada, fragmentação controlada, pipelining). Este é o épico que mais perde com o reescopo — compensar com casos byte-a-byte bem escolhidos.
- **Critérios de aceite:**
  - [ ] Casos positivos via `curl`: GET simples, POST com `Content-Length`, POST `--data-binary` chunked (`-H "Transfer-Encoding: chunked"`), GET com query string.
  - [ ] Casos negativos via `printf | nc`, verificando a **primeira linha** da resposta: método inválido → 400, `HTTP/2.0` → 505, HTTP/1.1 sem `Host` → 400, POST sem `Content-Length` → 411, URI > 8192 chars → 414, body acima do limite → 413, `Content-Length` duplicado → 400, `Content-Length` + `Transfer-Encoding` juntos → 400.
  - [ ] **Fragmentação:** a mesma request enviada byte a byte (com `sleep` entre os pedaços) produz a mesma resposta que enviada de uma vez.
  - [ ] **Pipelining:** duas requests num único `printf` recebem duas respostas. ← depende de [BUG-01-09](epic-01-motor-de-rede.md#bug-01-09--keep-alive-descarta-requests-em-pipelining) estar corrigido
  - [ ] Cada caso imprime `[PASS]`/`[FAIL]` e o script retorna exit code ≠ 0 se algum falhar.

---

## Resumo de tarefas

| ID | Tarefa | Status | Tamanho | Dependências |
|----|--------|--------|---------|-------------|
| E03-T01 | feed | ✅ | M | — |
| E03-T02 | parseRequestLine | ✅ | M | T01 |
| E03-T03 | parseHeaders | ✅ | M | T02 |
| E03-T04 | parseBodyByLength | ✅ | M | T03 |
| E03-T05 | parseBodyChunked | ✅ | L | T03 |
| E03-T06 | splitUri | ✅ | S | T02 |
| E03-T07 | keepAlive | ✅ | S | T03 |
| E03-T08 | take + reset | ✅ | S | T01 |
| E03-T09 | errorStatus | ✅ | S | T01–T05 |
| E03-T10 | Cobertura no test-edge-cases.sh | ❌ reescopada | M | T01–T09 |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`.

**Nenhum bug aberto neste épico.** O `RequestParser` foi o único módulo em que a auditoria
não encontrou defeito.

Há um bug **de fora** que anula um critério de aceite daqui:

- [BUG-01-09](epic-01-motor-de-rede.md#bug-01-09--keep-alive-descarta-requests-em-pipelining)
  — o `Client::onWritable` chama `parser_.reset()` no keep-alive, descartando os bytes
  residuais que o `take()` desta tarefa (E03-T08) preserva de propósito. O suporte a
  pipelining existe no parser e é jogado fora na camada de cima. A correção é no `Client`,
  não aqui — mas quem for validar E03-T10 precisa saber disso antes de escrever o caso de
  pipelining.
