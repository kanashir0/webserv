# Planejamento — Backlog do Webserv

> **Documento mestre** para o time de 3 pessoas. Use este arquivo como ponto de entrada para entender:
> 1. Em que ponto o projeto está hoje.
> 2. Quais são os épicos e o valor de cada um.
> 3. O que atacar primeiro.
> 4. Como dividir o trabalho entre M1, M2 e M3 sem que alguém fique bloqueado.

---

## 1. Mapa de épicos

**Legenda de status:** ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada

| # | Épico | Arquivo | Dono | Status |
|---|-------|---------|------|--------|
| 01 | Motor de Rede e Reactor Pattern | [`epic-01-motor-de-rede.md`](epic-01-motor-de-rede.md) | M1 | 🟡 5 ✅ / 8 ⚠️ / 0 ❌ |
| 02 | Parser de Configuração | [`epic-02-parser-configuracao.md`](epic-02-parser-configuracao.md) | M2 | 🟢 5 ✅ / 2 ⚠️ / 1 ❌ |
| 03 | Parser HTTP de Requisições | [`epic-03-parser-http.md`](epic-03-parser-http.md) | M2 | 🟢 9 ✅ / 0 ⚠️ / 1 ❌ |
| 04 | Resposta HTTP e Roteamento | [`epic-04-resposta-roteamento.md`](epic-04-resposta-roteamento.md) | M3 | 🟢 7 ✅ / 0 ⚠️ / 3 ❌ |
| 05 | Handlers HTTP (GET/POST/DELETE) | [`epic-05-handlers-http.md`](epic-05-handlers-http.md) | M3 | 🟡 4 ✅ / 0 ⚠️ / 2 ❌ |
| 06 | CGI | [`epic-06-cgi.md`](epic-06-cgi.md) | M1 + M3 | 🔴 0 ✅ / 1 ⚠️ / 8 ❌ |
| 07 | Bônus: Sessões e Cookies | [`epic-07-bonus-sessoes.md`](epic-07-bonus-sessoes.md) | M3 | 🟡 2 ✅ / 2 ⚠️ / 3 ❌ |
| 08 | Qualidade, Testes e Integração | [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md) | Todos | 🔴 0 ✅ / 0 ⚠️ / 8 ❌ |

**Total: 71 tarefas** — 32 ✅ · 13 ⚠️ · 26 ❌.
(69 do backlog original + 2 criadas na auditoria: E04-T10 e E06-T09. A contagem de "~62"
das versões anteriores deste documento estava errada.)

---

## 1.1. Estado atual — auditoria de 02/08/2026

> Feita sobre a branch `feat/request-pipeline`, com o binário compilado e **em execução**.
> `make re` compila limpo com `-Wall -Wextra -Werror -std=c++98 -pedantic`.

### O que o servidor faz hoje (verificado, não inferido)

| Cenário | Resultado | |
|---------|-----------|---|
| `GET /` | 200 + `www/index.html` | ✅ |
| `GET /__nope__` | 404 com `www/errors/404.html` | ✅ |
| `PUT /` | 405 | ✅ |
| body 2 MB contra limite de 1m | 413 | ✅ |
| `HTTP/2.0` | 505 | ✅ |
| HTTP/1.1 sem `Host` | 400 | ✅ |
| `/../../etc/passwd` | 403 | ✅ |
| keep-alive (2 requests, mesma conexão) | 200 + 200 | ✅ |
| **`GET /cgi-bin/hello.py`** | **200 com o código-fonte do script** | ❌ crítico |
| **`POST /upload/a.txt`** | **500** (`www/uploads` não existe no repo) | ❌ |
| **2 `server{}` na mesma porta** | funciona, mas com `BIND FALHOU` no log | ❌ |

### Onde o projeto está

O **caminho crítico do mandatory está de pé**: config → parser HTTP → roteamento → handlers
→ resposta funciona ponta a ponta, com as 5 restrições do subject respeitadas (um único
`poll()` em `EventLoop.cpp:37`, zero `fork()`, zero threads, I/O só dentro dos callbacks,
C++98 estrito).

A qualidade é desigual por módulo:

- **M2 (`ConfigParser`, `RequestParser`)** e **M3 (`Router`, `ResponseFactory`,
  `PathResolver`, handlers)** estão em nível de produção. O `RequestParser` implementa
  defesas contra request smuggling que nem foram pedidas.
- **M1 (`core/`, `cgi/`)** é o elo fraco: funciona no caminho feliz, mas **8 das 13 tarefas
  do E01 têm defeitos** que não cumprem os próprios critérios de aceite, e o CGI inteiro é
  stub.

### Bugs abertos, por severidade

**21 bugs catalogados.** Cada um tem entrada completa (sintoma, arquivo:linha, esperado) na
seção "Bugs e ajustes abertos" do épico correspondente.

| Severidade | ID | Resumo | Épico |
|---|---|---|---|
| 🔴 Crítica | BUG-05-01 | `GET` em script CGI devolve o **código-fonte** | [05](epic-05-handlers-http.md) |
| 🔴 Alta | BUG-01-02 | `accept()` lança exceção e derruba o servidor | [01](epic-01-motor-de-rede.md) |
| 🔴 Alta | BUG-01-03 | `onReadable` cria `Client` com FD -1 em loop infinito | [01](epic-01-motor-de-rede.md) |
| 🔴 Alta | BUG-01-04 | `Server::start` agrupa vhosts errado; bind redundante falha | [01](epic-01-motor-de-rede.md) |
| 🔴 Alta | BUG-01-05 | `Host: x:8080` nunca casa com `server_name` → vhost sempre default | [01](epic-01-motor-de-rede.md) |
| 🔴 Alta | BUG-01-06 | Timeout de cliente usa ms como s → 16 min em vez de 60 s | [01](epic-01-motor-de-rede.md) |
| 🔴 Alta | BUG-07-01 | Dois `SessionStore`; o `gc()` roda no vazio | [07](epic-07-bonus-sessoes.md) |
| 🔴 Alta | BUG-07-02 | `generateId` previsível — session fixation | [07](epic-07-bonus-sessoes.md) |
| 🟡 Média | BUG-01-07 | `~EventLoop` vaza os `IPollable*` no shutdown | [01](epic-01-motor-de-rede.md) |
| 🟡 Média | BUG-01-08 | Estados mortos no `Client`; `recv` ignora `errno` | [01](epic-01-motor-de-rede.md) |
| 🟡 Média | BUG-01-09 | Keep-alive descarta requests em pipelining | [01](epic-01-motor-de-rede.md) |
| 🟡 Média | BUG-05-02 | `www/uploads` não versionado → todo upload dá 500 | [05](epic-05-handlers-http.md) |
| 🟢 Baixa | BUG-01-01 | `setNonBlocking` apaga as flags do FD | [01](epic-01-motor-de-rede.md) |
| 🟢 Baixa | BUG-01-10 | `std::cout` de debug, código comentado, erros sem `errno` | [01](epic-01-motor-de-rede.md) |
| 🟢 Baixa | BUG-02-01 | 3 validações semânticas de config faltando | [02](epic-02-parser-configuracao.md) |
| 🟢 Baixa | BUG-02-02 | Diretiva é `return`; docs dizem `redirect` | [02](epic-02-parser-configuracao.md) |
| 🟢 Baixa | BUG-02-03 | O número da linha do `ParseError` nunca é exibido | [02](epic-02-parser-configuracao.md) |
| 🟢 Baixa | BUG-06-01 | `makeFromCgi` devolve 502 onde o critério pedia tolerância | [06](epic-06-cgi.md) |
| 🟢 Baixa | BUG-07-03 | `setCookie` sem `Path=/` por padrão | [07](epic-07-bonus-sessoes.md) |
| 🟢 Baixa | BUG-08-01 | `curl-suite` espera 403 onde o servidor responde 405 | [08](epic-08-qualidade-testes.md) |
| 🟢 Baixa | BUG-08-02 | `curl-suite` não prepara nem limpa `www/uploads` | [08](epic-08-qualidade-testes.md) |

---

## 2. Ordem de ataque recomendada

> Substitui o roadmap de sprints da seção 4, que virou histórico.

### 🔴 Fase A — Estabilizar o motor de rede (M1)

**Fechar BUG-01-01 a BUG-01-09.** É a fase que desbloqueia todas as outras.

Motivo: o Épico 06 (CGI) vai empilhar pipes, um segundo tipo de pollable e uma negociação de
ownership em cima do `EventLoop`. Fazer isso enquanto o `accept()` derruba o processo, o
timeout está 16× errado e o ownership dos pollables já é ambíguo transforma cada bug de CGI
numa caça ao fantasma. **Barato agora, caríssimo depois.**

Ordem sugerida dentro da fase: BUG-01-02 e BUG-01-03 juntos (um mascara o outro) →
BUG-01-04 → BUG-01-05 → BUG-01-06 → BUG-01-07 → BUG-01-08 → BUG-01-09 → BUG-01-01/10.

**Saída da fase:** `siege -c50 -t30s` (E08-T02) roda sem queda e `valgrind --track-fds`
(E08-T03) fecha limpo.

### 🟠 Fase B — CGI (M1 + M3, pair programming)

**Épico 06 inteiro**, incluindo a nova E06-T09 que resolve o BUG-05-01 crítico.

É o maior bloco pendente (9 tarefas, 1 XL) e o de maior peso no defense. Três decisões de
design já foram fixadas nas notas de escopo do épico — **leia-as antes de escrever a
primeira linha**: quem implementa `checkTimeout`, como os 2 pipes viram `IPollable`, e quem
é dono do `CgiHandler`.

Pré-requisitos de fora do épico: BUG-01-06, BUG-01-07 e BUG-01-01.

### 🟡 Fase C — Fechar o mandatory

- E04-T08 (`Request::cookie`) e E04-T10 (`Date`/`Server`/`Connection`)
- E05-T03 (caminho POST do CGI, depois da Fase B)
- BUG-05-02 (`.gitkeep` em `www/uploads`) e BUG-02-01/02/03
- E08-T01 e E08-T05 — as duas suítes de teste, agora ampliadas
- E08-T04 (browsers), E08-T06 (multi-server), E08-T08 (review final)

**Saída da fase: tag `v0.1`.**

### 🟣 Fase D — Bônus

**Épico 07.** Começar por BUG-07-01 (unificar o `SessionStore`) — sem isso, T03, T06 e T07
seriam escritas sobre uma base errada. Depois E07-T04 (BUG-07-02), E07-T06, E07-T07.

**Saída da fase: tag `v1.0`.**

> **Não antecipe a Fase D.** Metade do Épico 07 estar pronta é tentador, mas o CGI vale nota
> no defense e sessões não. O bônus só conta com o mandatory 100% verde.

---

## 3. Política de testes

> Definida em 02/08/2026. Detalhes e consequências em
> [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md).

O padrão de validação do projeto é **exclusivamente** por scripts shell contra o servidor
rodando:

| Script | Papel |
|--------|-------|
| `tests/scripts/curl-suite.sh` | Caminho feliz: status, headers, bodies, ciclo POST→GET→DELETE, vhosts, CGI |
| `tests/scripts/test-edge-cases.sh` | Casos-limite: malformados, fragmentação, limites, traversal, timeouts |
| `tests/scripts/run-siege.sh` | Carga e estabilidade |
| `tests/scripts/run-valgrind.sh` | Memória e FDs |

**Não haverá testes unitários em C++.** As quatro tarefas que previam isso (E02-T08,
E03-T10, E04-T09, E05-T06) foram reescopadas para casos nesses scripts, e sua cobertura foi
absorvida por E08-T01 e E08-T05. Nenhum diretório `tests/unit/` deve ser criado, e nenhuma
tarefa pode ser marcada `done` alegando cobertura unitária.

---

## 4. Roadmap original em sprints (histórico)

> Mantido para registro. **Superado pela seção 2.** Os Sprints 0, 1 e 2 foram entregues; o
> Sprint 3 ficou parcial (o CGI não começou) e o Sprint 4 não começou.

- **Sprint 0 — Setup** ✅ esqueleto, headers, configs base, scripts.
- **Sprint 1 — Núcleo independente** ✅ `EventLoop`, parser de config e HTTP, `Response` e fábricas.
- **Sprint 2 — Integração mínima E2E** ✅ `curl http://localhost:8080/` responde 200; roteamento e handlers ligados.
- **Sprint 3 — POST + CGI + estresse** 🟡 parcial: POST/upload feito, **CGI não começou**, siege não rodou.
- **Sprint 4 — Polish, bônus e defense** ❌ não começou.

---

## 5. Distribuição por membro

### M1 — Core de Rede
- **Épico 01:** todas as 13 tarefas — **8 a reabrir** (o grosso da Fase A).
- **Épico 06:** T03, T04, T05, T07 — o bloco XL do projeto.
- **Épico 08:** T02 (siege), T03 parcial.

### M2 — Parsers e Configuração
- **Épico 02:** 8 tarefas — T05 e T07 a reabrir, T08 reescopada.
- **Épico 03:** 10 tarefas — **9 fechadas**, só T10 (reescopada) pendente. Módulo mais sólido do projeto.
- **Épico 08:** T01 (curl-suite, escopo ampliado), T06 (multi-server).

> M2 está com a menor dívida técnica. Bom candidato a assumir E08-T01/T05 cedo, já que as
> suítes de teste destravam a validação das Fases A e B.

### M3 — Lógica HTTP, Sessões, CGI env
- **Épico 04:** 10 tarefas — 7 fechadas; falta T08 (cookie), T09 (reescopada), T10 (nova).
- **Épico 05:** 6 tarefas — 4 fechadas; falta T03 (CGI) e T06 (reescopada).
- **Épico 06:** T01, T02, T06, T09 (nova) — a parte de env e roteamento do CGI.
- **Épico 07:** 7 tarefas — bônus, Fase D.

---

## 6. Bloqueios cruzados a antecipar

| Quem espera | Pelo quê | Mitigação |
|-------------|----------|-----------|
| M1 (E06-T03) | `CgiEnv::asEnvp` populado (M3, E06-T02) | Pair programming; M1 pode testar com um envp fixo de 3 variáveis |
| M3 (E06-T09) | Nada — só precisa de `loc.cgi`, que já existe | Pode começar **antes** da Fase B e já mata o BUG-05-01 crítico |
| M1+M3 (E06-T07) | Decisão de ownership | **Já decidida** na nota de escopo de E06-T07; não reabrir |
| M3 (E07-T06) | E04-T08 (`Request::cookie`) + BUG-07-01 | Sequência interna do M3 |
| M1 (E06-T05) | BUG-01-06 corrigido | Fase A antes da Fase B |
| Todos (E08-T02/T03) | Fase A completa | Rodar antes só produz falhas já conhecidas |

**Regra de ouro:** se você está bloqueado por mais de 1 dia esperando alguém, mocka a
dependência e siga. Os contratos (`.hpp`) são suficientes para isso.

---

## 7. Convenções de pull request

- **Tamanho:** PRs idealmente < 500 linhas.
- **Título:** `feat(<epico>): <verbo> <objeto>` — ex: `fix(core): strip port from Host header`.
- **Descrição:**
  - Link para a tarefa ou bug: `Closes E03-T05` / `Fixes BUG-01-05`.
  - Como testar (comando exato).
  - Checklist:
    - [ ] `make re` sem warnings.
    - [ ] Caso novo no `curl-suite.sh` ou `test-edge-cases.sh`.
    - [ ] Valgrind sem leaks no caso testado.
    - [ ] Sem `printf`/`std::cout` de debug.
    - [ ] Critérios de aceite marcados no arquivo do épico.
- **Reviewer:** pelo menos 1 dos outros 2 membros.
- **Merge:** squash, mensagem em inglês imperativo.

---

## 8. Tamanhos (t-shirt sizing)

| Tamanho | Tempo | Exemplo |
|---------|-------|---------|
| **S** | até 2h | `Socket::setNonBlocking` |
| **M** | meio dia a 1 dia | `parseHeaders` |
| **L** | 1–2 dias | `EventLoop::runOnce`, `parseBodyChunked` |
| **XL** | 3+ dias | `CgiHandler::start` |

---

## 9. Riscos identificados

| Risco | Status em 02/08/2026 |
|-------|----------------------|
| Loop com `poll()` despachando errado | ✅ mitigado — dispatch correto, um único `poll()` |
| CGI deixando zombies | ⏳ em aberto — o CGI não começou; `waitpid(WNOHANG)` previsto em E06-T05 |
| Path traversal não bloqueado | ✅ mitigado — `PathResolver::normalizePath` bloqueia antes do filesystem, verificado |
| Fragmentação quebrando o parser | ✅ mitigado — parser lida com qualquer granularidade |
| FD leak em error paths | 🔴 **materializado** — BUG-01-07 (shutdown) |
| Múltiplos `Set-Cookie` perdidos no `std::map` | ✅ mitigado — campo `cookies_` separado |
| Pair programming de CGI atrasando ambos | ⏳ em aberto — marcar slot fixo para a Fase B |
| **Divulgação de código-fonte via CGI** | 🔴 **materializado** — BUG-05-01, não estava previsto |
| **Virtual hosting inoperante com clientes reais** | 🔴 **materializado** — BUG-01-05, não estava previsto |

---

## 10. Definition of Done

### Por tarefa
- [ ] Critérios de aceite todos marcados no arquivo do épico.
- [ ] Compila sem warnings (`-Wall -Wextra -Werror -std=c++98 -pedantic`).
- [ ] Pelo menos um caso novo em `curl-suite.sh` ou `test-edge-cases.sh` (ver seção 3).
- [ ] Valgrind no cenário coberto: 0 bytes lost, 0 FDs leaked.
- [ ] Sem `printf`/`std::cout` de debug, sem código comentado.
- [ ] Pelo menos 1 reviewer aprovou o PR.
- [ ] Status atualizado de ❌/⚠️ para ✅ no arquivo do épico.

### Por épico
- [ ] Todas as suas tarefas em ✅ e **nenhum bug aberto** na seção "Bugs e ajustes abertos".
- [ ] Critério de "épico pronto" do topo do arquivo validado na prática.
- [ ] Merge para `main` via PR de finalização.

---

## 11. Quick links

- [Subject 42 — Webserv](https://cdn.intra.42.fr/pdf/pdf/960/en.subject.pdf)
- Architecture: [`docs/architecture.md`](../docs/architecture.md)
- Git workflow: [`docs/git-workflow.md`](../docs/git-workflow.md)
- Testing: [`docs/testing.md`](../docs/testing.md)
- README arquitetural: [`README.md`](../README.md)

---

> **Lembrete:** este backlog é vivo. O quadro de status da seção 1.1 reflete a auditoria de
> **02/08/2026** — reveja-o a cada retrospectiva, e atualize o status da tarefa no arquivo do
> épico junto com o PR que a fecha, não depois.
