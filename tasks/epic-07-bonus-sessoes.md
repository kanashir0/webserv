# Épico 07 — Bônus: Sessões e Cookies

> **Dono primário:** Membro 3 (M3)
> **Branch:** `feat/http-logic` (ou sub-branch `feat/sessions` se preferir isolar)
> **Valor entregue:** capacidade de manter estado entre requisições do mesmo usuário via cookie `WEBSERV_SESSION`. Habilita features básicas de aplicações web: carrinho de compras, login, preferências.
> **Critério de "épico pronto":** primeiro acesso ao servidor recebe `Set-Cookie: WEBSERV_SESSION=<id>`; acessos subsequentes do mesmo cliente reutilizam a sessão; sessões expiram após TTL (default 1h).
> **Pré-requisito:** mandatory (Épicos 01–06) deve estar completo antes de iniciar este épico.

> **Status do épico (auditoria de 02/08/2026):** 🟡 **2 ✅ / 2 ⚠️ / 3 ❌** — mais adiantado do
> que se esperava para um bônus: a mecânica de `Session`, o CRUD do `SessionStore` e o
> `Set-Cookie` múltiplo já existem. Mas **nada disso está ligado a uma request** — o
> `attachSessionCookie` é um stub, e há dois `SessionStore` distintos no processo
> ([BUG-07-01](#bug-07-01--existem-dois-sessionstore-e-o-gc-roda-no-errado)), o que precisa
> ser resolvido antes de qualquer outra tarefa daqui.
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

> **Lembrete de prioridade:** o `PLANNING.md` coloca este épico na **Fase D**, depois do
> mandatory fechado. O fato de metade estar pronta não é motivo para antecipar — o CGI
> (Épico 06) vale nota, sessões não.

---

## ✅ E07-T01 — Implementar `Session` (getters/setters/touch/expired)

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/session/Session.cpp`, `include/session/Session.hpp`
- **Dependências:** nenhuma
- **Descrição:** completar a classe `Session` com `has`, `get`, `set`, `erase`, `touch(ttl)`, `expired(now)`. `touch` atualiza `expiresAt_ = now + ttl`. `expired` retorna `expiresAt_ != 0 && now >= expiresAt_`.
- **Critérios de aceite:**
  - [x] `set("k", "v")` e depois `get("k") == "v"`.
  - [x] `get("inexistente")` retorna `""`.
  - [x] `touch(3600)` faz `expiresAt() == time(0) + 3600`.
  - [x] `expired(time(0))` é `false` recém-tocado, `true` após `expiresAt`.
  - [x] Construtor padrão existe (necessário para `std::map<string, Session>`).
  - [x] `expiresAt_ == 0` significa "nunca expira" — sessão recém-construída sem `touch` não é coletada por engano.

---

## ✅ E07-T02 — Implementar `SessionStore::getOrCreate`, `find`, `drop`

- **Owner:** M3
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/session/SessionStore.cpp`, `include/session/SessionStore.hpp`
- **Dependências:** E07-T01, E07-T04
- **Descrição:**
  - `getOrCreate(id)`: se `id == ""` → gera novo via `generateId()`. Se sessão existe → `touch(ttl_)`, retorna referência. Se não existe → cria nova `Session(id)`, insere, retorna referência.
  - `find(id)`: busca sem criar; retorna `NULL` se não existe.
  - `drop(id)`: remove do mapa (logout).
- **Critérios de aceite:**
  - [x] Retorna `Session&` — modificações persistem no mapa.
  - [x] `getOrCreate("")` sempre cria nova sessão com ID novo.
  - [x] `getOrCreate(existente)` renova TTL.
  - [x] `find()` não renova TTL.
  - [x] `drop()` em ID inexistente é no-op.
  - [x] A sessão nova já nasce com `touch(ttlSeconds_)` aplicado, então nunca fica com `expiresAt_ == 0` por acidente.

> **Ressalva:** a unicidade do ID depende de `generateId()`, que hoje é um contador
> previsível — ver [E07-T04](#-e07-t04--implementar-sessionstoregenerateid-via-devurandom)
> e [BUG-07-02](#bug-07-02--generateid-é-previsível-session-fixation).

---

## ⚠️ E07-T03 — Implementar `SessionStore::gc` + integração com `EventLoop`

- **Owner:** M3
- **Status:** ⚠️ REABRIR — o `gc()` está correto, mas roda no `SessionStore` errado. Ver [BUG-07-01](#bug-07-01--existem-dois-sessionstore-e-o-gc-roda-no-errado)
- **Tamanho:** M
- **Arquivos afetados:** `src/session/SessionStore.cpp`, `src/core/EventLoop.cpp`, `src/core/Server.cpp`, `src/main.cpp`
- **Dependências:** E07-T02
- **Descrição:** `gc()` percorre o mapa removendo sessões expiradas, usando o padrão correto de `erase` em C++98 (salvar o iterador antes de incrementar). Chamar `gc()` a cada tick do `EventLoop`.
- **Critérios de aceite:**
  - [x] Padrão de iteração correto: `it++` salvo em `del` antes do `erase` — sem iterator invalidation.
  - [x] `gc()` com muitas sessões ativas não trava o loop (O(n) por tick é aceitável).
  - [x] Integração feita via interface `ITickable` + `EventLoop::setTickHandler`, sem acoplar o loop ao módulo de sessões. **Bom desenho** — o `EventLoop` não sabe o que é uma sessão.
  - [ ] O `gc()` roda sobre o mesmo store que atende as requests. ← **não roda**: o `Server` registra o *seu* `sessions_` como tick handler, mas quem o `Router` usa é outro objeto, criado no `main()`
  - [ ] Sessão criada com TTL=2s e `gc()` chamado após 3s é removida. ← não verificável enquanto o store errado for coletado

---

## ❌ E07-T04 — Implementar `SessionStore::generateId` via `/dev/urandom`

- **Owner:** M3
- **Status:** ❌ PENDENTE — existe um placeholder com contador previsível (`src/session/SessionStore.cpp:47-57`). Ver [BUG-07-02](#bug-07-02--generateid-é-previsível-session-fixation)
- **Tamanho:** M
- **Arquivos afetados:** `src/session/SessionStore.cpp`
- **Dependências:** nenhuma
- **Descrição:** abrir `/dev/urandom`, ler 16 bytes, converter cada byte para 2 dígitos hex (32 chars total). IDs previsíveis são vulnerabilidade de **session fixation** — não usar `rand()` nem contador.
- **Critérios de aceite:**
  - [ ] ID gerado tem exatamente 32 caracteres hex.
  - [ ] 1000 IDs gerados são todos únicos.
  - [ ] Caracteres são apenas `[0-9a-f]`.
  - [ ] Falha ao abrir `/dev/urandom` lança exceção ou loga erro grave — **nunca** cair para um fallback inseguro em silêncio.
  - [ ] O FD de `/dev/urandom` é gerenciado por `FileDescriptor` (RAII) e não vaza.

> **Nota sobre o `poll()`:** ler `/dev/urandom` é a única leitura de arquivo do projeto que
> acontece fora de um callback de `IPollable`. Isso **não** viola a restrição do subject —
> a regra vale para os FDs registrados no `poll()` (sockets e pipes), e `/dev/urandom` nunca
> bloqueia. O mesmo já vale para os `std::ifstream` do `ResponseFactory` e do `ConfigParser`.
> Vale ter a resposta pronta para o defense.

---

## ⚠️ E07-T05 — Implementar `Response::setCookie` (com suporte a múltiplos)

- **Owner:** M3
- **Status:** ⚠️ REABRIR — falta só o default `Path=/`. Ver [BUG-07-03](#bug-07-03--setcookie-não-emite-path-por-padrão)
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Response.cpp`, `include/http/Response.hpp`
- **Dependências:** E04-T01
- **Descrição:** adicionar `Set-Cookie: name=value; options` à resposta. Como `HeaderMap` é `std::map` (não suporta múltiplos valores por chave), foi adotada a **opção (c)** sugerida na tarefa: um campo separado `cookies_` (`StringVec`), emitido no `toString()`.
- **Critérios de aceite:**
  - [x] `setCookie("a", "1")` e `setCookie("b", "2")` emitem 2 headers `Set-Cookie` distintos.
  - [x] Suporte a `Max-Age`, `Expires`, `HttpOnly`, `Secure`, `SameSite` via o parâmetro `options`.
  - [x] Valor com `\r\n` ou caracteres de controle é sanitizado (`sanitizeHeaderValue`) — proteção contra cookie/header injection.
  - [x] `setHeader("Set-Cookie", ...)` também é redirecionado para `cookies_`, então nenhum caminho perde cookie por colisão de chave no `std::map`.
  - [ ] Default `options = "Path=/"`. ← ver [BUG-07-03](#bug-07-03--setcookie-não-emite-path-por-padrão)

---

## ❌ E07-T06 — Implementar `Router::attachSessionCookie`

- **Owner:** M3
- **Status:** ❌ PENDENTE — stub vazio (`src/http/Router.cpp:78-80`). **Bloqueada** por [E04-T08](epic-04-resposta-roteamento.md#-e04-t08--implementar-requestcookie-leitura-de-cookies) e [BUG-07-01](#bug-07-01--existem-dois-sessionstore-e-o-gc-roda-no-errado)
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Router.cpp`
- **Dependências:** E07-T02, E07-T05, E04-T08, E04-T06, BUG-07-01
- **Descrição:** após o handler retornar a `Response`, ler `req.cookie("WEBSERV_SESSION")`. Chamar `sessions_.getOrCreate(id)`. Se o ID era vazio ou desconhecido (sessão nova) → `resp.setCookie("WEBSERV_SESSION", session.id(), "Path=/; HttpOnly")`. Se o ID já existia, apenas o TTL foi renovado (sem header novo).
- **Critérios de aceite:**
  - [ ] Primeiro acesso (sem cookie) retorna `Set-Cookie: WEBSERV_SESSION=<id>`.
  - [ ] Segundo acesso (com cookie válido) **não** retorna `Set-Cookie` — evita rotacionar o ID a cada request.
  - [ ] Cookie com ID que não existe no store gera nova sessão e novo `Set-Cookie` (usar `find()` para distinguir "existe" de "não existe" **antes** do `getOrCreate`, que sempre cria).
  - [ ] Cookie expirado é tratado como ausente.
  - [ ] O cookie sai com `Path=/` e `HttpOnly` (defesa mínima contra roubo por XSS).
  - [ ] `Router::route` chama `attachSessionCookie` em **todos** os caminhos de retorno, inclusive nos de erro (404/405/500) — hoje o método tem 6 `return` distintos.

---

## ❌ E07-T07 — Testes de sessões ponta-a-ponta

- **Owner:** M3
- **Status:** ❌ PENDENTE
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/test-sessions.sh` (novo), `tests/configs/basic.conf`
- **Dependências:** E07-T01–E07-T06
- **Descrição:** suite usando `curl -c cookies.txt -b cookies.txt`, seguindo o padrão de testes do projeto (scripts shell, ver [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md)).
- **Critérios de aceite:**
  - [ ] 1ª request grava o cookie; 2ª request reutiliza e **não** recebe `Set-Cookie` novo.
  - [ ] O ID no `cookies.txt` bate com `[0-9a-f]{32}` (valida E07-T04).
  - [ ] Cookie forjado/inexistente gera sessão nova e novo `Set-Cookie`.
  - [ ] Sessão expira: com TTL curto (10 s), após o prazo a request seguinte recebe ID novo.
  - [ ] O cookie traz `Path=/` e `HttpOnly`.
  - [ ] 1000 IDs coletados em requests sem cookie são todos distintos (`sort -u | wc -l`).
  - [ ] Sem leaks no Valgrind após 100 sessões criadas e expiradas.
  - [ ] Cada teste imprime `[PASS]`/`[FAIL]`; exit code ≠ 0 se algum falhar.

---

## Resumo de tarefas

| ID | Tarefa | Status | Tamanho | Dependências |
|----|--------|--------|---------|-------------|
| E07-T01 | Session basics | ✅ | S | — |
| E07-T02 | SessionStore CRUD | ✅ | M | T01, T04 |
| E07-T03 | SessionStore::gc | ⚠️ BUG-07-01 | M | T02 |
| E07-T04 | generateId via urandom | ❌ BUG-07-02 | M | — |
| E07-T05 | Response::setCookie | ⚠️ BUG-07-03 | M | E04-T01 |
| E07-T06 | Router::attachSessionCookie | ❌ | M | T02, T05, E04-T08 |
| E07-T07 | Testes ponta-a-ponta | ❌ | M | T01–T06 |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`.

### BUG-07-01 — Existem dois `SessionStore` e o `gc()` roda no errado

- **Origem:** E07-T03
- **Onde:** `src/main.cpp:26-28` e `src/core/Server.cpp:100` / `include/core/Server.hpp:65`
- **Sintoma:** o processo cria **duas** instâncias independentes de `SessionStore`:

  ```cpp
  // main.cpp — este vai para o Router, que é quem atende as requests
  SessionStore sessions;
  Router       router(sessions);
  Server       server(configs, router);

  // Server.hpp — o Server tem o seu próprio, membro por valor
  SessionStore sessions_;

  // Server.cpp:100 — e é ESTE que é registrado no tick do EventLoop
  loop_.setTickHandler(&sessions_);
  ```

  O `gc()` roda a cada tick sobre o store do `Server`, que está **sempre vazio**. O store do
  `Router` — o único que teria sessões de verdade — nunca é coletado. Hoje o sintoma é
  invisível porque `attachSessionCookie` é um stub e ninguém cria sessão; assim que E07-T06
  for implementada, o servidor passa a acumular sessões para sempre (vazamento de memória
  lento, proporcional ao número de visitantes).
- **Esperado:** um único `SessionStore`. A correção mais limpa é o `Server` deixar de ter o
  membro próprio e receber o store por referência, do mesmo jeito que já recebe o `Router` —
  mantém o `main()` como o único dono e não muda a interface do tick.
- **Severidade:** **Alta** — é pré-requisito de E07-T03, T06 e T07. Corrigir **antes** de
  qualquer outra tarefa deste épico, senão elas são escritas sobre uma base errada.

### BUG-07-02 — `generateId` é previsível (session fixation)

- **Origem:** E07-T04
- **Onde:** `src/session/SessionStore.cpp:47-57`
- **Sintoma:** o gerador é um placeholder marcado com `TODO`:

  ```cpp
  static unsigned long counter = 0;
  ++counter;
  std::string id = "sess-";
  for (int i = 0; i < 16; ++i) {
      unsigned long v = (counter * 2654435761UL + i * 97UL) & 0xFu;
      ...
  ```

  Determinístico a partir de um contador que **reinicia em zero a cada boot**. Qualquer
  pessoa que rode o binário obtém exatamente a mesma sequência de IDs e pode assumir a
  sessão de outro usuário. Além disso, os 16 dígitos derivam todos do mesmo `counter`, então
  a entropia real é próxima de zero — e o prefixo `sess-` faz o ID ter 21 caracteres, não os
  32 hex que o critério pede.
- **Por que é perigoso:** o código **parece funcionar**. Os IDs saem diferentes entre si, os
  testes de "sessão nova recebe cookie" passam, e nada chama atenção até alguém auditar.
- **Esperado:** E07-T04, com `/dev/urandom`. Enquanto não for feito, o bônus **não pode ser
  apresentado como pronto**.
- **Severidade:** Alta se o bônus for entregue; nula enquanto o épico estiver parado (nenhuma
  sessão é criada hoje).

### BUG-07-03 — `setCookie` não emite `Path` por padrão

- **Origem:** E07-T05
- **Onde:** `include/http/Response.hpp` (default do parâmetro `options`)
- **Sintoma:** o default é `""`, não `"Path=/"` como pedia o critério. Sem `Path`, o browser
  limita o cookie ao diretório da request que o criou — uma sessão iniciada em `/upload/x`
  não acompanha o usuário em `/`.
- **Esperado:** default `"Path=/"`, ou o `attachSessionCookie` (E07-T06) sempre passar
  `"Path=/; HttpOnly"` explicitamente. A segunda opção é a preferível — deixa a decisão de
  escopo com quem cria o cookie.
- **Severidade:** Baixa — ajuste de uma linha, resolvido junto de E07-T06.
