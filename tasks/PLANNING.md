# Planejamento — Backlog do Webserv

> **Documento mestre** para o time de 3 pessoas. Use este arquivo como ponto de entrada para entender:
> 1. Quais são os épicos do projeto e o valor de cada um.
> 2. Como priorizar e em qual ordem atacar.
> 3. Como dividir as tarefas entre M1, M2 e M3 sem que alguém fique bloqueado.
> 4. Como acompanhar a evolução por sprint.

---

## 1. Mapa de épicos

| # | Épico | Arquivo | Dono primário | Foco |
|---|-------|---------|---------------|------|
| 01 | Motor de Rede e Reactor Pattern | [`epic-01-motor-de-rede.md`](epic-01-motor-de-rede.md) | M1 | Sockets, `poll()`, ciclo de vida de cliente, RAII de FDs |
| 02 | Parser de Configuração | [`epic-02-parser-configuracao.md`](epic-02-parser-configuracao.md) | M2 | Leitura de `.conf` estilo Nginx |
| 03 | Parser HTTP de Requisições | [`epic-03-parser-http.md`](epic-03-parser-http.md) | M2 | Bytes do socket → `Request` validado |
| 04 | Resposta HTTP e Roteamento | [`epic-04-resposta-roteamento.md`](epic-04-resposta-roteamento.md) | M3 | `Response`, `Router`, `ResponseFactory` |
| 05 | Handlers HTTP (GET/POST/DELETE) | [`epic-05-handlers-http.md`](epic-05-handlers-http.md) | M3 | Lógica dos métodos HTTP |
| 06 | CGI | [`epic-06-cgi.md`](epic-06-cgi.md) | M1 + M3 | Execução de scripts dinâmicos não-bloqueante |
| 07 | Bônus: Sessões e Cookies | [`epic-07-bonus-sessoes.md`](epic-07-bonus-sessoes.md) | M3 | `WEBSERV_SESSION` cookie + TTL |
| 08 | Qualidade, Testes e Integração | [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md) | Todos | siege, valgrind, browsers, edge cases, defense checklist |

**Total de tarefas:** ~62 tarefas distribuídas. Tamanhos: 22 S, 26 M, 11 L, 1 XL.

---

## 2. Princípios de priorização

1. **Caminho crítico primeiro.** O servidor só sobe ponta-a-ponta quando E01 + E02 + E03 + E04 (parcial) + E05 (parcial) estão integrados. Tudo isso é mandatory v0.1.
2. **Trabalho paralelo desde o dia 1.** O esqueleto do projeto e os contratos (`.hpp`) já estão prontos. Cada membro começa pelo seu épico próprio sem depender dos outros.
3. **Mocks e testes unitários antes da integração.** M2 testa o parser com strings inline; M3 testa handlers com `Request` mockado; M1 testa o EventLoop com mocks de `IPollable`. Isso evita o gargalo de "esperar o outro membro terminar".
4. **CGI é integração coordenada.** Não comece o E06 antes de E03 (RequestParser) e E05 (PostHandler) estarem prontos. CGI é o ponto de maior risco — agendar pair programming.
5. **Bônus depois do mandatory.** Não inicie E07 antes de o defense do mandatory estar verde. Resistir à tentação de fazer "só uma feature de sessão" no meio do desenvolvimento principal.
6. **Qualidade é contínua, não no final.** E08 é executado em paralelo desde o sprint 2 — cada PR deve passar por valgrind antes de merge.

---

## 3. Roadmap em sprints (sugestão de 4 sprints de 1 semana)

> Ajuste a duração conforme disponibilidade do time. Os 4 sprints abaixo assumem ~10h/semana por pessoa.

### 🟢 Sprint 0 — Setup (já concluído)
**Status:** ✅ Esqueleto pronto, branches definidas, arquivos `.cpp` em stub.
- Headers (`.hpp`) definidos em `include/`.
- Stubs em `.cpp` compilam sem warnings.
- Configs base em `conf/` e `tests/configs/`.
- Scripts de teste em `tests/scripts/`.

### 🟡 Sprint 1 — Núcleo independente (semana 1)
**Objetivo:** cada membro destrava o caminho crítico do seu pilar isoladamente.

| Membro | Tarefas | Tamanho total |
|--------|---------|--------------|
| **M1** | E01-T01, T02, T03 (Socket), E01-T04, T05, T06 (EventLoop) | ~2L + 4S |
| **M2** | E02-T01–T03 (parser config básico), E03-T01–T03 (parser HTTP básico) | ~2L + 4M |
| **M3** | E04-T01–T05 (Response + Factory), E07-T01 (Session basics) | ~5M + 1S |

**Marco:** ao fim do sprint, M1 tem `EventLoop` rodando com mocks; M2 parseia `.conf` e request line + headers; M3 gera `Response` válido para qualquer cenário.

### 🟠 Sprint 2 — Integração mínima E2E (semana 2)
**Objetivo:** primeira request real ponta-a-ponta funciona.

| Membro | Tarefas | Tamanho total |
|--------|---------|--------------|
| **M1** | E01-T07 (ListeningSocket), E01-T08 (Server::start), E01-T09, T10 (Client) | ~2L + 2M |
| **M2** | E03-T04, T05 (body parsing), E02-T04, T05 (location + validação), E02-T06 (findLocation) | ~2L + 1M + 1S |
| **M3** | E04-T06, T07 (Router), E05-T01 (GetHandler), E05-T04 (DeleteHandler) | ~1L + 1M + 1S |

**Marco:** `curl http://localhost:8080/` retorna 200 com `index.html`; `curl -X DELETE` funciona; primeiro `curl-suite.sh` verde.

### 🔴 Sprint 3 — POST + CGI + estresse (semana 3)
**Objetivo:** mandatory completo, incluindo o componente mais difícil (CGI).

| Membro | Tarefas | Tamanho total |
|--------|---------|--------------|
| **M1** | E06-T03 (CgiHandler::start), E06-T04, T05 (pipes + timeout), E01-T11–T13 | ~1XL + 1L + 2M + 1S |
| **M2** | E03-T06–T10 (finalizar parser HTTP), E02-T07–T08 (testes config) | ~2M + 3S |
| **M3** | E05-T02, T03 (PostHandler), E06-T01, T02, T06 (CgiEnv + makeFromCgi), E05-T05, T06 | ~1L + 3M + 1S |
| **Pair** | E06-T07 (integração Client+CGI) | ~1M (M1+M3 juntos) |

**Marco:** `curl --data-binary @big.bin http://localhost:8080/upload` funciona; `curl http://localhost:8080/cgi-bin/post_echo.py` ecoa body; `siege -c20 -t10s` sem queda. **Tag `v0.1` candidata.**

### 🟣 Sprint 4 — Polish, bônus e defense (semana 4)
**Objetivo:** valgrind 100% limpo, browsers OK, sessões funcionando, defense pronto.

| Membro | Tarefas | Tamanho total |
|--------|---------|--------------|
| **M1** | E08-T02 (siege), E08-T03 (valgrind do core), revisão final do EventLoop | ~2M |
| **M2** | E08-T01 (curl-suite), E08-T06 (multi-server), code review M1+M3 | ~2M |
| **M3** | E07 completo (sessions), E08-T03 (valgrind dos handlers) | ~1M + 5S/M |
| **Todos** | E08-T04, T05, T07, T08 | ~1M + 2S |

**Marco:** Tag `v1.0` (com bônus). Defense pronto.

---

## 4. Distribuição final por membro (visão consolidada)

### M1 — Core de Rede (16 tarefas)
- **Épico 01 (próprio):** E01-T01 → T13 (todas as 13 tarefas).
- **Épico 06 (compartilhado):** E06-T03, T04, T05, T07.
- **Épico 08:** E08-T02 (siege), E08-T03 parcial.

**Total estimado:** ~3 XL/L + 7 M + 6 S.

### M2 — Parsers e Configuração (19 tarefas)
- **Épico 02 (próprio):** E02-T01 → T08 (todas as 8 tarefas).
- **Épico 03 (próprio):** E03-T01 → T10 (todas as 10 tarefas).
- **Épico 08:** E08-T01 (curl-suite), E08-T06 (multi-server).

**Total estimado:** ~3 L + 11 M + 5 S.

### M3 — Lógica HTTP, Sessões, CGI env (24 tarefas)
- **Épico 04 (próprio):** E04-T01 → T09 (todas as 9 tarefas).
- **Épico 05 (próprio):** E05-T01 → T06 (todas as 6 tarefas).
- **Épico 06 (compartilhado):** E06-T01, T02, T06, T07 (parcial).
- **Épico 07 (próprio):** E07-T01 → T07 (todas as 7 tarefas).
- **Épico 08:** E08-T03 parcial.

**Total estimado:** ~3 L + 16 M + 5 S.

### Tarefas compartilhadas (Todos)
- E06-T08 (testes CGI ponta-a-ponta).
- E08-T03 (valgrind — cada um valida seu domínio).
- E08-T04 (browsers — manual).
- E08-T05 (edge cases).
- E08-T07 (documentação).
- E08-T08 (code review final).

---

## 5. Bloqueios cruzados a antecipar

| Quem espera | Pelo quê | De quem | Mitigação |
|-------------|----------|---------|-----------|
| M1 (E01-T08 Server::start) | `vector<ServerConfig>` válido | M2 (E02-T03) | M1 pode começar com config hardcoded mockada; integra depois. |
| M1 (E01-T09 Client::onReadable) | `RequestParser::feed` | M2 (E03-T01) | M1 mocka um parser "fake" que retorna `COMPLETE` após 1 KB. |
| M1 (E01-T10 Client::onWritable) | `Response::toString` | M3 (E04-T01) | M1 monta string HTTP fixa para teste. |
| M3 (E05-T01 handlers) | `LocationConfig::findLocation` | M2 (E02-T06) | M3 cria mock de `LocationConfig` em tests/. |
| M3 (E07-T06 attachSessionCookie) | E04-T08 (Request::cookie) | M3 (próprio) | Sequencia interna do M3. |
| M1 (E06-T03 CgiHandler::start) | `CgiEnv::asEnvp` | M3 (E06-T02) | Pair programming na semana 3. |
| Todos (E08-T08 review) | Tudo pronto | Todos | Marca data fixa para review final no sprint 4. |

**Regra de ouro:** se você está bloqueado por mais de 1 dia esperando alguém, mocka a dependência e sigam em frente. Os contratos (`.hpp`) são suficientes para isso.

---

## 6. Convenções de pull request

- **Tamanho:** PRs idealmente < 500 linhas (já em `docs/git-workflow.md`).
- **Título:** `feat(<epico>): <verb> <objeto>` — ex: `feat(parser): add chunked body support`.
- **Descrição:**
  - Link para a tarefa: `Closes E03-T05`.
  - Como testar (comando exato).
  - Checklist:
    - [ ] `make re` sem warnings (`-Wall -Wextra -Werror`).
    - [ ] Valgrind sem leaks no caso testado.
    - [ ] Sem `printf` de debug.
    - [ ] Critérios de aceite da tarefa marcados.
- **Reviewer:** pelo menos 1 dos outros 2 membros.
- **Merge:** squash, mensagem em inglês imperativo.

---

## 7. Como interpretar os tamanhos (t-shirt sizing)

| Tamanho | Tempo estimado | Exemplo de tarefa |
|---------|---------------|-------------------|
| **S** | até 2h | `Socket::setNonBlocking` (uma syscall + tratamento de erro) |
| **M** | meio dia até 1 dia | `parseHeaders` (state machine, validações) |
| **L** | 1–2 dias | `EventLoop::runOnce`, `parseBodyChunked` (lógica complexa, vários edge cases) |
| **XL** | 3+ dias | `CgiHandler::start` (fork + pipes + execve + integração com loop) |

Tamanhos são **estimativas iniciais**, não compromissos. Reavaliar no início de cada sprint.

---

## 8. Riscos identificados e plano de mitigação

| Risco | Probabilidade | Impacto | Mitigação |
|-------|---------------|---------|-----------|
| Loop com `poll()` despachando errado e gerando recv() bloqueante | Médio | Alto (reprovação) | Code review obrigatório do E01-T04; teste unitário com FDs sem dados. |
| CGI deixando processos zombies | Alto | Alto | `waitpid(WNOHANG)` no loop + `kill` em timeout. Validar com `ps aux \| grep python`. |
| Path traversal não bloqueado em GET/DELETE | Médio | Crítico (segurança) | E05-T05 dedicada; revisão cruzada obrigatória. |
| Fragmentação de requests gerando bug no parser | Alto | Médio | E03-T10 testa fragmentação byte-a-byte. |
| FD leak em error paths | Médio | Alto | RAII (`FileDescriptor`); valgrind `--track-fds=yes` em CI. |
| Headers múltiplos `Set-Cookie` perdidos no `std::map` | Médio | Médio | E07-T05 usa campo separado `vector<string>`. |
| Pair programming de CGI atrasando ambos | Médio | Médio | Marcar slot fixo de 4h no sprint 3 para M1+M3 sentarem juntos. |

---

## 9. Definition of Done (por tarefa)

Uma tarefa só pode ser marcada como `done` quando **todos** abaixo são verdadeiros:
- [ ] Critérios de aceite da tarefa estão todos marcados.
- [ ] Compila sem warnings (`-Wall -Wextra -Werror -std=c++98 -pedantic`).
- [ ] Valgrind no cenário coberto: 0 bytes lost, 0 FDs leaked.
- [ ] Sem `printf`/`std::cout` de debug deixados no código.
- [ ] Sem código comentado (use git para ressuscitar se precisar).
- [ ] Pelo menos 1 reviewer aprovou o PR.
- [ ] Merge na branch da feature; merge para `main` ao final do épico.

---

## 10. Definition of Done (por épico)

Um épico só pode ser marcado como `done` quando:
- [ ] Todas as suas tarefas estão `done`.
- [ ] Critério de "épico pronto" descrito no topo do arquivo está validado.
- [ ] Merge para `main` realizado via PR de finalização.
- [ ] Tag intermediária criada se aplicável (ex: `v0.1-mandatory` no fim do E05).

---

## 11. Quick links

- [Subject 42 — Webserv](https://cdn.intra.42.fr/pdf/pdf/960/en.subject.pdf) (consulta rápida)
- Architecture: [`docs/architecture.md`](../docs/architecture.md)
- Git workflow: [`docs/git-workflow.md`](../docs/git-workflow.md)
- Testing: [`docs/testing.md`](../docs/testing.md)
- Plano original: [`docs/ideia_inicial.md`](../docs/ideia_inicial.md)
- README arquitetural completo: [`README.md`](../README.md)

---

> **Lembrete final:** este backlog é vivo. Ajuste prazos, redistribua tarefas e recalibre tamanhos a cada retrospectiva semanal. O objetivo é entregar um servidor HTTP/1.1 que passe no defense da 42 — não seguir o plano à risca se a realidade pedir mudanças.
