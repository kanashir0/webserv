# Épico 07 — Bônus: Sessões e Cookies

> **Dono primário:** Membro 3 (M3)
> **Branch:** `feat/http-logic` (ou sub-branch `feat/sessions` se preferir isolar)
> **Valor entregue:** capacidade de manter estado entre requisições do mesmo usuário via cookie `WEBSERV_SESSION`. Habilita features básicas de aplicações web: carrinho de compras, login, preferências.
> **Critério de "épico pronto":** primeiro acesso ao servidor recebe `Set-Cookie: WEBSERV_SESSION=<id>`; acessos subsequentes do mesmo cliente reutilizam a sessão; sessões expiram após TTL (default 1h).
> **Pré-requisito:** mandatory (Épicos 01–06) deve estar completo antes de iniciar este épico.

---

## E07-T01 — Implementar `Session` (getters/setters/touch/expired)

- **Owner:** M3
- **Tamanho:** S
- **Arquivos afetados:** `src/session/Session.cpp`, `include/session/Session.hpp`
- **Dependências:** nenhuma
- **Descrição:** completar a classe `Session` com `has`, `get`, `set`, `erase`, `touch(ttl)`, `expired(now)`. `touch` atualiza `expiresAt_ = now + ttl`. `expired` retorna `expiresAt_ != 0 && now >= expiresAt_`.
- **Critérios de aceite:**
  - [ ] `set("k", "v")` e depois `get("k") == "v"`.
  - [ ] `get("inexistente")` retorna `""`.
  - [ ] `touch(3600)` faz `expiresAt() == time(0) + 3600` (com tolerância de 1s).
  - [ ] `expired(time(0))` é `false` recém-tocado, `true` após `expiresAt`.
  - [ ] Construtor padrão existe (necessário para `std::map<string, Session>`).

---

## E07-T02 — Implementar `SessionStore::getOrCreate`, `find`, `drop`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/session/SessionStore.cpp`, `include/session/SessionStore.hpp`
- **Dependências:** E07-T01, E07-T04
- **Descrição:**
  - `getOrCreate(id)`: se `id == ""` → gera novo via `generateId()`. Se sessão existe → `touch(ttl_)`, retorna referência. Se não existe → cria nova `Session(id)`, insere, retorna referência.
  - `find(id)`: busca sem criar; retorna `nullptr` se não existe.
  - `drop(id)`: remove do mapa (logout).
- **Critérios de aceite:**
  - [ ] Retorna `Session&` (referência) — modificações persistem no mapa.
  - [ ] `getOrCreate("")` sempre cria nova sessão com ID único.
  - [ ] `getOrCreate(existente)` renova TTL.
  - [ ] `find()` não renova TTL (uso para validação inicial).
  - [ ] `drop()` em ID inexistente é no-op (não erro).

---

## E07-T03 — Implementar `SessionStore::gc` + integração com `EventLoop`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/session/SessionStore.cpp`, `src/core/EventLoop.cpp`
- **Dependências:** E07-T02
- **Descrição:** `gc()` percorre o mapa removendo sessões com `expired(now) == true`, usando padrão correto de iteração com `erase` em C++98 (salvar iterador antes de incrementar). Chamar `gc()` em `EventLoop::runOnce()` a cada tick.
- **Critérios de aceite:**
  - [ ] Sessão criada com TTL=2s e `gc()` chamado após 3s remove a sessão.
  - [ ] Chamar `gc()` com 1000 sessões ativas não trava o loop (operação O(n) aceitável).
  - [ ] Sem iterator invalidation — testar com Valgrind.
  - [ ] Performance: GC não roda a cada poll() — pode ser chamado a cada N ticks (decisão do M1, default a cada tick é aceitável dado o TTL alto).

---

## E07-T04 — Implementar `SessionStore::generateId` via `/dev/urandom`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/session/SessionStore.cpp`
- **Dependências:** nenhuma
- **Descrição:** abrir `/dev/urandom`, ler 16 bytes, converter cada byte para 2 dígitos hex (32 chars total). IDs previsíveis são vulnerabilidade de **session fixation** — não usar `rand()` ou contador.
- **Critérios de aceite:**
  - [ ] ID gerado tem exatamente 32 caracteres hex.
  - [ ] 1000 IDs gerados são todos únicos.
  - [ ] Caracteres são apenas `[0-9a-f]`.
  - [ ] Falha em abrir `/dev/urandom` lança exceção ou loga erro grave (não fallback inseguro).

---

## E07-T05 — Implementar `Response::setCookie` (com suporte a múltiplos)

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Response.cpp`, `src/http/Response.hpp`
- **Dependências:** E04-T01
- **Descrição:** adicionar `Set-Cookie: name=value; options` à resposta. Como `HeaderMap` é `std::map` (não suporta múltiplos valores por chave), implementar uma das estratégias:
  - (a) usar `std::multimap` para um campo dedicado `cookies_`;
  - (b) acumular múltiplos `Set-Cookie` separados por sequência específica e splittar no `toString()`;
  - (c) campo separado `setCookies_` (`vector<string>`) emitido em `toString()`.
  Recomendado: opção (c) — mais simples, sem hacks.
- **Critérios de aceite:**
  - [ ] `setCookie("a", "1")` e `setCookie("b", "2")` emitem 2 headers `Set-Cookie` distintos no output.
  - [ ] Default `options = "Path=/"`.
  - [ ] Suporte a `Max-Age`, `Expires`, `HttpOnly`, `Secure`, `SameSite` via `options`.
  - [ ] Valor com `;` ou `\r\n` é rejeitado (proteção contra cookie injection).

---

## E07-T06 — Implementar `Router::attachSessionCookie`

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Router.cpp`
- **Dependências:** E07-T02, E07-T05, E04-T08, E04-T06
- **Descrição:** após o handler retornar `Response`, ler `req.cookie("WEBSERV_SESSION")`. Chamar `sessions_.getOrCreate(id)`. Se o ID era vazio (sessão nova) → `resp.setCookie("WEBSERV_SESSION", session.id())`. Se o ID já existia, apenas o TTL foi renovado (sem header novo).
- **Critérios de aceite:**
  - [ ] Primeiro acesso (sem cookie) retorna `Set-Cookie: WEBSERV_SESSION=<id>`.
  - [ ] Segundo acesso (com cookie) **não** retorna `Set-Cookie` (evita rotacionar ID).
  - [ ] Cookie inválido (ID que não existe no store) gera nova sessão e novo `Set-Cookie`.
  - [ ] Cookie expirado é tratado como ausente (gera novo).

---

## E07-T07 — Testes de sessões ponta-a-ponta

- **Owner:** M3
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/test-sessions.sh` (novo)
- **Dependências:** E07-T01–E07-T06
- **Descrição:** suite de testes usando `curl -c cookies.txt -b cookies.txt`:
  - 1ª request salva cookie, 2ª request reutiliza.
  - Sessão expira após TTL configurado curto (10s).
  - Cookie inválido gera novo.
  - Logout (`drop`) limpa a sessão.
- **Critérios de aceite:**
  - [ ] Pelo menos 6 cenários cobertos com asserts via `grep` no output.
  - [ ] Funciona com `tests/configs/basic.conf` ajustada com TTL curto.
  - [ ] Sem leaks no Valgrind após 100 sessões criadas e expiradas.

---

## Resumo de tarefas

| ID | Tarefa | Tamanho | Dependências |
|----|--------|---------|-------------|
| E07-T01 | Session basics | S | — |
| E07-T02 | SessionStore CRUD | M | T01, T04 |
| E07-T03 | SessionStore::gc | M | T02 |
| E07-T04 | generateId via urandom | M | — |
| E07-T05 | Response::setCookie | M | E04-T01 |
| E07-T06 | Router::attachSessionCookie | M | T02, T05, E04-T08 |
| E07-T07 | Testes ponta-a-ponta | M | T01–T06 |
