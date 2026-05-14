# Épico 03 — Parser HTTP de Requisições

> **Dono primário:** Membro 2 (M2)
> **Branch:** `feat/parsers`
> **Valor entregue:** transformação de bytes brutos do socket em objeto `Request` validado e imutável. É o contrato entre o motor de rede (M1, que fornece bytes) e a lógica HTTP (M3, que consome `Request`). Sem este épico, o servidor não entende HTTP.
> **Critério de "épico pronto":** parser aceita requisições HTTP/1.0 e HTTP/1.1 válidas (incluindo `Transfer-Encoding: chunked`), rejeita malformadas com o status correto (400/411/413/414/505), e suporta envio fragmentado (recv parcial).

---

## E03-T01 — Implementar `RequestParser::feed` (entrada de dados)

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`, `include/http/RequestParser.hpp`
- **Dependências:** nenhuma (módulo independente; usa apenas `common/`)
- **Descrição:** appendar `data[0..n]` no buffer interno `buf_`. Dependendo de `state_`, despachar para o método de parsing correspondente (`parseRequestLine`, `parseHeaders`, `parseBodyByLength`, `parseBodyChunked`). Retornar `FeedResult::NEED_MORE`, `COMPLETE`, ou um dos códigos de erro.
- **Critérios de aceite:**
  - [ ] Aceita `data` em qualquer granularidade (1 byte, 100 bytes, request inteira).
  - [ ] `state_` avança apenas quando há bytes suficientes; senão retorna `NEED_MORE`.
  - [ ] `maxBody` é validado durante o parsing — não acumula body acima do limite antes de rejeitar.
  - [ ] Buffer não cresce indefinidamente — bytes consumidos são removidos (`buf_.erase(0, n)` ou índice de leitura).

---

## E03-T02 — Implementar `parseRequestLine`

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T01
- **Descrição:** procurar `\r\n` no `buf_`. A linha antes deve ter formato `METHOD URI VERSION`. Validar: método é token ASCII (sem espaços), URI começa com `/` e tem ≤ 8192 chars, versão é `HTTP/1.0` ou `HTTP/1.1`. Preencher `building_.method_`, `uri_`, `version_`.
- **Critérios de aceite:**
  - [ ] `GET / HTTP/1.1\r\n` é parseado com sucesso.
  - [ ] URI > 8192 chars retorna `URI_TOO_LONG` (414).
  - [ ] Método com espaço ou caractere inválido retorna `BAD_REQUEST` (400).
  - [ ] `HTTP/2.0` retorna `HTTP_VERSION_UNSUPPORTED` (505).
  - [ ] Linha sem `\r\n` ainda no buffer → `NEED_MORE`.
  - [ ] Suporta apenas `\r\n` como separador (não aceita só `\n`).

---

## E03-T03 — Implementar `parseHeaders`

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T02
- **Descrição:** ler linhas `Key: Value\r\n` até encontrar a linha vazia `\r\n`. Inserir em `building_.headers_` (que usa `CaseInsensitiveLess`). Ao encontrar a linha vazia, analisar `Content-Length` e `Transfer-Encoding` para decidir próximo estado: `BODY_LENGTH`, `BODY_CHUNKED` ou `DONE` (sem body).
- **Critérios de aceite:**
  - [ ] `Content-Length: 42` → `state_ = BODY_LENGTH`, `contentLength_ = 42`.
  - [ ] `Transfer-Encoding: chunked` → `state_ = BODY_CHUNKED`.
  - [ ] Header malformado (sem `:` ou com nome vazio) → `BAD_REQUEST`.
  - [ ] Espaços ao redor do valor são removidos: `Host:  localhost  ` → `headers["Host"] == "localhost"`.
  - [ ] Headers duplicados: o último vence (comportamento de `std::map::operator[]`).
  - [ ] Total de headers > 100 ou tamanho > 8KB → `BAD_REQUEST` (proteção contra abuso).

---

## E03-T04 — Implementar `parseBodyByLength` + validação `maxBody`

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T03
- **Descrição:** ler exatamente `contentLength_` bytes do buffer e copiar para `building_.body_`. Se `contentLength_ > maxBody` → `BODY_TOO_LARGE`. Se POST sem `Content-Length` e sem `Transfer-Encoding` → `LENGTH_REQUIRED` (411).
- **Critérios de aceite:**
  - [ ] `Content-Length: 0` é válido (body vazio).
  - [ ] Body parcial (chegaram só 5 de 42 bytes) → `NEED_MORE`.
  - [ ] `Content-Length` excede `maxBody` → `BODY_TOO_LARGE` mesmo sem ter recebido todo o body.
  - [ ] POST sem `Content-Length` nem `Transfer-Encoding` → `BAD_REQUEST` (`errorStatus = 411`).
  - [ ] GET com body é aceito (alguns clientes enviam) — não é erro, mas o body é ignorado pela maioria dos handlers.

---

## E03-T05 — Implementar `parseBodyChunked` (Transfer-Encoding: chunked)

- **Owner:** M2
- **Tamanho:** L
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T03
- **Descrição:** implementar protocolo chunked: cada chunk começa com tamanho em hex `\r\n`, depois `<size>` bytes, depois `\r\n`. Chunk de tamanho 0 (`0\r\n\r\n`) finaliza. Acumular total em `building_.body_` validando contra `maxBody`.
- **Critérios de aceite:**
  - [ ] Body chunked simples (3 chunks + terminator) é decodificado corretamente.
  - [ ] Tamanho em hex maiúsculo e minúsculo (`A`, `a`, `0xA`) funciona — usar `strtol(s, NULL, 16)`.
  - [ ] Chunk com tamanho inválido (não-hex) → `BAD_REQUEST`.
  - [ ] Soma dos chunks excede `maxBody` → `BODY_TOO_LARGE` (mesmo a meio do parsing).
  - [ ] Trailers chunked (headers após body) são lidos e ignorados (ou armazenados, opcional).
  - [ ] Recebimento parcial em qualquer ponto do chunked retorna `NEED_MORE`.

---

## E03-T06 — Implementar `splitUri` (separação de path e query)

- **Owner:** M2
- **Tamanho:** S
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T02
- **Descrição:** após parsear `uri_`, encontrar primeiro `?` e dividir em `path_` e `query_`. URIs sem `?` têm `query_ = ""`.
- **Critérios de aceite:**
  - [ ] `/upload/foo.png?thumb=1` → `path_="/upload/foo.png"`, `query_="thumb=1"`.
  - [ ] `/sem-query` → `path_="/sem-query"`, `query_=""`.
  - [ ] `/?` → `path_="/"`, `query_=""`.
  - [ ] Múltiplos `?` (inválido tecnicamente, mas tolerável) → divide no primeiro.
  - [ ] Path com `..` é mantido como está — sanitização contra path traversal é responsabilidade dos handlers (M3).

---

## E03-T07 — Implementar `Request::keepAlive`

- **Owner:** M2
- **Tamanho:** S
- **Arquivos afetados:** `src/http/Request.cpp`
- **Dependências:** E03-T03
- **Descrição:** ler header `Connection`. Em HTTP/1.1, default = keep-alive a menos que `Connection: close`. Em HTTP/1.0, default = close a menos que `Connection: keep-alive`. Comparação case-insensitive.
- **Critérios de aceite:**
  - [ ] `HTTP/1.1` sem header `Connection` → `keepAlive() == true`.
  - [ ] `HTTP/1.1` com `Connection: close` → `false`.
  - [ ] `HTTP/1.0` sem header → `false`.
  - [ ] `HTTP/1.0` com `Connection: keep-alive` → `true` (case-insensitive).
  - [ ] Funciona com valores em qualquer capitalização (`Keep-Alive`, `KEEP-ALIVE`, etc.).

---

## E03-T08 — Implementar `RequestParser::take` e `reset`

- **Owner:** M2
- **Tamanho:** S
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T01
- **Descrição:** `take()` retorna o `Request` construído (`building_`) e reinicia o parser internamente. `reset()` limpa todos os estados (buffer, contadores, building_) para reutilização da mesma instância em conexões keep-alive.
- **Critérios de aceite:**
  - [ ] Após `take()`, `state_ == METHOD` e `buf_` vazio.
  - [ ] Bytes residuais no buffer (de uma próxima request em pipelining) são preservados em `take()` — implementação correta evita perder dados.
  - [ ] `reset()` chamado a qualquer momento volta o parser ao estado inicial limpo.

---

## E03-T09 — Tratamento de erros e `errorStatus`

- **Owner:** M2
- **Tamanho:** S
- **Arquivos afetados:** `src/http/RequestParser.cpp`
- **Dependências:** E03-T01–E03-T05
- **Descrição:** ao retornar qualquer `FeedResult` de erro, setar `errorStatus_` com o código HTTP correspondente. `errorStatus()` retorna o valor para o `Client::onReadable()` construir a resposta.
- **Critérios de aceite:**
  - [ ] `BAD_REQUEST` → `errorStatus = 400`.
  - [ ] `URI_TOO_LONG` → `errorStatus = 414`.
  - [ ] `BODY_TOO_LARGE` → `errorStatus = 413`.
  - [ ] `HTTP_VERSION_UNSUPPORTED` → `errorStatus = 505`.
  - [ ] POST sem `Content-Length` → `errorStatus = 411`.

---

## E03-T10 — Testes unitários do `RequestParser`

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `tests/unit/test_request_parser.cpp` (novo) ou `tests/scripts/test-parser.sh`
- **Dependências:** E03-T01–E03-T09
- **Descrição:** suite de testes alimentando `feed()` com strings que simulam requisições reais e fragmentadas. Cobrir: GET simples, POST com Content-Length, POST chunked, requisições inválidas, fragmentação byte-a-byte.
- **Critérios de aceite:**
  - [ ] Pelo menos 15 casos de teste (positivos e negativos).
  - [ ] Caso de fragmentação: `feed()` chamado 1 byte por vez resulta em mesmo `Request` que `feed()` em uma chamada só.
  - [ ] Caso de pipelining: duas requests no mesmo buffer são parseadas em sequência (com `take()` entre elas).
  - [ ] Suite passa sem warnings com `-Wall -Wextra -Werror`.

---

## Resumo de tarefas

| ID | Tarefa | Tamanho | Dependências |
|----|--------|---------|-------------|
| E03-T01 | feed | M | — |
| E03-T02 | parseRequestLine | M | T01 |
| E03-T03 | parseHeaders | M | T02 |
| E03-T04 | parseBodyByLength | M | T03 |
| E03-T05 | parseBodyChunked | L | T03 |
| E03-T06 | splitUri | S | T02 |
| E03-T07 | keepAlive | S | T03 |
| E03-T08 | take + reset | S | T01 |
| E03-T09 | errorStatus | S | T01–T05 |
| E03-T10 | Testes unitários | M | T01–T09 |
