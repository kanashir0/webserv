# Épico 08 — Qualidade, Testes e Integração

> **Donos:** Todos (rotação de responsabilidade)
> **Branches:** trabalho em sub-branches `chore/test-<descricao>` ou `fix/<bug>` quando necessário.
> **Valor entregue:** garantia de que o servidor passa o defense da 42 — sem leaks, sem crashes, sem comportamentos inesperados sob estresse, alinhado com a RFC 7230. Sem este épico, todo o trabalho dos outros épicos pode ser reprovado.
> **Critério de "épico pronto":**
> - `./webserv` passa no `curl-suite.sh` completo (todos os cenários).
> - `siege -c50 -t30s` mantém Availability ≥ 99.5% sem crash.
> - `valgrind --leak-check=full --track-fds=yes` mostra `0 bytes lost` e `0 FDs leaked`.
> - Chrome e Firefox renderizam o site sem erros no devtools.
> - Checklist do subject 42 100% verde.

> **Status do épico (auditoria de 02/08/2026):** 🔴 **0 ✅ / 0 ⚠️ / 8 ❌** — nada iniciado.
> O `curl-suite.sh` existe com 4 casos (um deles errado, ver
> [BUG-08-01](#bug-08-01--o-curl-suite-espera-403-onde-o-servidor-responde-405)) e os scripts
> de siege e valgrind estão no repositório mas nunca foram executados contra o servidor real.
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

---

## 📌 Política de testes do projeto

> **Definida em 02/08/2026. Vale para todos os épicos.**

O padrão de validação do webserv é **exclusivamente** por scripts shell que exercitam o
servidor pela rede:

| Script | Papel |
|--------|-------|
| `tests/scripts/curl-suite.sh` | Caminho feliz e comportamento esperado: status codes, headers, bodies, ciclo POST→GET→DELETE, virtual hosting, CGI |
| `tests/scripts/test-edge-cases.sh` | Casos-limite e malformados: requests inválidas, fragmentação, limites de tamanho, path traversal, timeouts |
| `tests/scripts/run-siege.sh` | Carga e estabilidade |
| `tests/scripts/run-valgrind.sh` | Memória e FDs |

**Não haverá testes unitários em C++.** As quatro tarefas que previam isso
(E02-T08, E03-T10, E04-T09, E05-T06) foram reescopadas para casos nestes scripts.
Motivos: a 42 avalia o comportamento do servidor, não a cobertura unitária; o projeto não
tem harness de teste nem framework, e montar um consumiria mais tempo do que a suíte
inteira de integração; e um teste que sobe o binário de verdade pega classes inteiras de bug
(FDs, `poll()`, ordem de eventos) que um teste de unidade nunca veria.

**Consequências práticas:**
- Nenhum diretório `tests/unit/` deve ser criado.
- Nenhuma tarefa pode ser marcada `done` alegando cobertura unitária.
- Todo comportamento novo entra com pelo menos um caso no `curl-suite.sh` ou no
  `test-edge-cases.sh`.
- Ambos os scripts precisam ser **idempotentes** (limpar o que sujarem) e devolver exit code
  ≠ 0 em qualquer falha, para poderem rodar em `make test`.

---

## ❌ E08-T01 — Estender `tests/scripts/curl-suite.sh` (smoke completo)

- **Owner:** M2 (mantenedor da suite)
- **Status:** ❌ PENDENTE — existem 4 casos hoje, um deles com expectativa errada ([BUG-08-01](#bug-08-01--o-curl-suite-espera-403-onde-o-servidor-responde-405)). **Escopo ampliado** pela política de testes acima: esta tarefa absorve a cobertura de E04-T09 e E05-T06
- **Tamanho:** L (era M — cresceu com o reescopo das tarefas de teste unitário)
- **Arquivos afetados:** `tests/scripts/curl-suite.sh`
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** ampliar a suite de smoke tests com asserts automatizados (status code esperado, presença/ausência de header, conteúdo do body via `grep`). Cobrir todos os cenários de `docs/testing.md`.
- **Critérios de aceite:**
  - [ ] Cobertura mínima: GET 200, GET 404, GET 403 (autoindex off), POST 201, DELETE 204, PUT 405, body 413, URI 414, body chunked, redirect 301/302, virtual hosting (`-H Host:`).
  - [ ] **Setup/teardown:** cria `www/uploads` se faltar e limpa o conteúdo antes e depois (cross-ref [BUG-05-02](epic-05-handlers-http.md#bug-05-02--wwwuploads-não-existe-no-repositório) e [BUG-08-02](#bug-08-02--o-curl-suite-não-prepara-nem-limpa-wwwuploads)).
  - [ ] Ciclo completo POST → GET → DELETE → GET sobre o mesmo arquivo (absorve E05-T06).
  - [ ] Headers verificados, não só status: `Allow` no 405, `Location` no redirect, `Content-Type` por extensão, `Content-Location` no 201 (absorve E04-T09).
  - [ ] Casos de CGI: GET com query, POST com body, e o assert de que **nenhum GET a `.py` devolve código-fonte** (cross-ref [BUG-05-01](epic-05-handlers-http.md#bug-05-01--get-em-script-cgi-devolve-o-código-fonte)).
  - [ ] Cada teste imprime `[PASS]` ou `[FAIL]` colorido.
  - [ ] Script retorna exit code 0 se tudo passa, ≠ 0 se algum falha. ← o `make test` atual tem um `|| true` que engole a falha; remover.
  - [ ] Sobe e derruba o servidor sozinho, ou documenta claramente que espera um servidor já rodando.

---

## ❌ E08-T02 — Stress test com siege (Availability ≥ 99.5%)

- **Owner:** M1 (dono do motor de rede)
- **Status:** ❌ PENDENTE — `run-siege.sh` existe mas nunca foi executado contra o servidor
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/run-siege.sh`
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** rodar `siege -c50 -t30s http://localhost:8080/` e validar que Availability ≥ 99.5% e que o servidor continua atendendo após o teste. Ajustar o script para parsear o output e falhar se a métrica não for atingida.
- **Critérios de aceite:**
  - [ ] `siege -c50 -t30s` reporta Availability ≥ 99.5%.
  - [ ] Servidor continua atendendo após o stress (`curl` retorna 200).
  - [ ] Sem zombies, sem FDs vazados (`lsof -p <pid>` antes/depois com diff ≤ 5).
  - [ ] Versão para CGI: `siege -c20 -t10s .../cgi-bin/env_dump.py` sem queda.

> **⚠️ Não rodar antes da Fase A.** Com [BUG-01-02](epic-01-motor-de-rede.md#bug-01-02--socketacceptconnection-lança-exceção-em-erro-não-eagain)
> em aberto, um `ECONNABORTED` sob carga derruba o processo — este teste vai falhar por um
> motivo já conhecido, e o tempo gasto investigando é desperdiçado. Fechar os bugs do E01
> primeiro.

---

## ❌ E08-T03 — Validação de memory leaks e FD leaks com Valgrind

- **Owner:** Todos (cada um valida sua área)
- **Status:** ❌ PENDENTE — `run-valgrind.sh` existe mas nunca foi executado
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/run-valgrind.sh`
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** rodar o servidor sob `valgrind --leak-check=full --track-fds=yes --error-exitcode=1`. Submeter cargas representativas (curl-suite + algumas conexões CGI). Verificar `definitely lost: 0 bytes` e nenhum FD aberto além dos do próprio valgrind.
- **Critérios de aceite:**
  - [ ] `definitely lost: 0 bytes`, `indirectly lost: 0 bytes`. ← **vai falhar hoje** por [BUG-01-07](epic-01-motor-de-rede.md#bug-01-07--eventloop-não-libera-os-ipollable-restantes-no-shutdown): o `~EventLoop` não deleta os `IPollable*` restantes, então todo `Client` vivo no momento do `SIGINT` vaza
  - [ ] `still reachable` tolerado se for de bibliotecas do sistema (libstdc++).
  - [ ] FDs abertos no fim: apenas 0, 1, 2.
  - [ ] Script falha com exit ≠ 0 se houver leak.
  - [ ] Roda sob shutdown gracioso (SIGINT) — o `kill -9` não conta.

---

## ❌ E08-T04 — Testes de compatibilidade com browsers

- **Owner:** Todos (manual)
- **Status:** ❌ PENDENTE
- **Tamanho:** S
- **Arquivos afetados:** `tests/manual-browser.md` (novo)
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** abrir `http://localhost:8080/` em Chrome, Firefox, Safari (se disponível) e validar renderização, headers e devtools.
- **Critérios de aceite:**
  - [ ] Página index renderiza.
  - [ ] DevTools (`F12 → Network`) sem requests com erro.
  - [ ] Headers HTTP corretos: `Content-Type`, `Content-Length`, **`Date`**. ← o `Date` **não é emitido hoje**; depende de [E04-T10](epic-04-resposta-roteamento.md#-e04-t10--emitir-date-server-e-connection-close-na-resposta)
  - [ ] Upload via formulário multipart funciona.
  - [ ] Cookies (se Épico 07 implementado) persistem entre reloads.
  - [ ] Documento `tests/manual-browser.md` lista o passo a passo.
  - [ ] Cada membro do time valida ao menos 1 browser.

> **⚠️ Depende de [BUG-01-05](epic-01-motor-de-rede.md#bug-01-05--matchvirtualhost-não-remove-a-porta-do-header-host).**
> Browsers mandam `Host: localhost:8080` (com porta), que hoje nunca casa com o
> `server_name` — todo acesso de browser cai no vhost default. Com um único `server{}` isso
> passa despercebido; com virtual hosting, o teste de browser vai mostrar o site errado.

---

## ❌ E08-T05 — Bateria de edge cases (`test-edge-cases.sh`)

- **Owner:** Todos
- **Status:** ❌ PENDENTE — arquivo ainda não existe. **Escopo ampliado** pela política de testes: esta tarefa absorve a cobertura de E02-T08 e E03-T10
- **Tamanho:** L (era M)
- **Arquivos afetados:** `tests/scripts/test-edge-cases.sh` (novo)
- **Dependências:** Épicos 01–05
- **Descrição:** automatizar os casos-limite. Muitos deles o `curl` não consegue produzir (request propositalmente malformada, fragmentação byte a byte, pipelining) — usar `printf | nc` nesses casos e verificar a primeira linha da resposta.
- **Critérios de aceite:**
  - [ ] Body > `clientMaxBodySize` → 413. ← já verificado manualmente, falta automatizar
  - [ ] URI > 8192 chars → 414.
  - [ ] POST sem `Content-Length` nem `Transfer-Encoding` → 411.
  - [ ] `HTTP/2.0` → 505. ← já verificado manualmente
  - [ ] HTTP/1.1 sem header `Host` → 400. ← já verificado manualmente
  - [ ] `Transfer-Encoding: chunked` → 200.
  - [ ] `Content-Length` duplicado → 400; `Content-Length` + `Transfer-Encoding` juntos → 400 (absorve E03-T10).
  - [ ] Header com byte nulo → 400.
  - [ ] Path traversal (`--path-as-is /../../etc/passwd`) → 403, nunca 200. ← já verificado manualmente
  - [ ] Fragmentação: request enviada byte a byte produz a mesma resposta que enviada de uma vez (absorve E03-T10).
  - [ ] Pipelining: duas requests num único envio recebem duas respostas. ← depende de [BUG-01-09](epic-01-motor-de-rede.md#bug-01-09--keep-alive-descarta-requests-em-pipelining)
  - [ ] Conexão fechada no meio da request → servidor não trava nem cai.
  - [ ] Conexão ociosa recebe `408` dentro do prazo configurado. ← depende de [BUG-01-06](epic-01-motor-de-rede.md#bug-01-06--timeout-de-cliente-usa-milissegundos-como-segundos); hoje o prazo real é ~16 min
  - [ ] Configs inválidas fazem o binário sair com código ≠ 0 e mensagem com o número da linha (absorve E02-T08; depende de [BUG-02-03](epic-02-parser-configuracao.md#bug-02-03--o-número-da-linha-do-parseerror-nunca-chega-ao-usuário)).
  - [ ] Cada caso tem o resultado esperado documentado no próprio script.

---

## ❌ E08-T06 — Testes de configurações múltiplas

- **Owner:** M2
- **Status:** ❌ PENDENTE
- **Tamanho:** M
- **Arquivos afetados:** `tests/configs/multi-server.conf`, `tests/scripts/test-multi-server.sh` (novo)
- **Dependências:** Épico 01, Épico 02
- **Descrição:** validar virtual hosting de verdade — dois `server{}` na **mesma** porta com `server_name` diferentes, e servers em portas diferentes.
- **Critérios de aceite:**
  - [ ] Servidor inicia com sucesso usando `tests/configs/multi-server.conf`.
  - [ ] Asserts via `grep` no body verificam que o vhost correto respondeu.
  - [ ] Host desconhecido cai no vhost default (primeiro declarado).
  - [ ] `Host` **com porta** (`site-a.local:8080`, como browsers mandam) roteia igual ao sem porta. ← ver [BUG-01-05](epic-01-motor-de-rede.md#bug-01-05--matchvirtualhost-não-remove-a-porta-do-header-host)
  - [ ] `Host` com caixa diferente (`SITE-A.LOCAL`) roteia igual.
  - [ ] Nenhum `BIND FALHOU` no log de startup. ← ver [BUG-01-04](epic-01-motor-de-rede.md#bug-01-04--serverstart-agrupa-vhosts-errado-e-sempre-tenta-um-bind-redundante)

> **⚠️ A config atual não testa o caso principal.** `tests/configs/multi-server.conf` tem
> dois servers em **portas diferentes** (8080 e 8081), o que não exercita o
> compartilhamento de listening socket. Acrescentar um par na **mesma** porta — foi assim
> que BUG-01-04 apareceu na auditoria.

---

## ❌ E08-T07 — Documentação operacional final

- **Owner:** Todos (rotação)
- **Status:** ❌ PENDENTE
- **Tamanho:** S
- **Arquivos afetados:** `README.md`, `docs/`, `CLAUDE.md`
- **Dependências:** todos os épicos
- **Descrição:** garantir que README e docs refletem o estado real do projeto: comandos, exemplos de configuração, troubleshooting.
- **Critérios de aceite:**
  - [ ] README com seção "Quick Start" funcional (copy-paste roda).
  - [ ] Exemplos de `.conf` documentados **e corretos** — hoje `CLAUDE.md` e `README.md` documentam a diretiva como `redirect`, mas o parser implementa `return` (ver [BUG-02-02](epic-02-parser-configuracao.md#bug-02-02--a-diretiva-de-redirect-chama-se-return-mas-a-documentação-diz-redirect)).
  - [ ] Documentar a política de testes deste épico no README.
  - [ ] Troubleshooting comum (porta em uso, sem permissão, valgrind reports).
  - [ ] Tag `v0.1` criada quando o mandatory estiver pronto; `v1.0` quando o bônus estiver.

---

## ❌ E08-T08 — Code review final + checklist do subject 42

- **Owner:** Todos (cross-review)
- **Status:** ❌ PENDENTE — a auditoria de 02/08/2026 é um ensaio parcial disto; ver os resultados abaixo
- **Tamanho:** M
- **Arquivos afetados:** todos
- **Dependências:** Épicos 01–07 completos
- **Descrição:** auditoria final antes do defense. Cada membro revisa o código dos outros dois.
- **Critérios de aceite:**
  - [ ] Apenas 1 `poll()` em todo o projeto. ← **já verificado em 02/08/2026:** só `src/core/EventLoop.cpp:37`
  - [ ] Nenhum `read()`/`write()`/`recv()`/`send()` fora dos callbacks de `IPollable`. ← **já verificado:** conforme
  - [ ] Apenas 1 `fork()` (em `CgiHandler::start`). ← **já verificado:** hoje há **zero**, porque o CGI é stub; revalidar após o Épico 06
  - [ ] Sem `pthread_*`, sem `std::thread`. ← **já verificado:** conforme
  - [ ] Compila com `c++ -Wall -Wextra -Werror -std=c++98 -pedantic` sem warnings. ← **já verificado:** `make re` limpo
  - [ ] Sem `printf`/`std::cout` de debug. ← **falha hoje:** `src/core/Server.cpp:97` (ver [BUG-01-10](epic-01-motor-de-rede.md#bug-01-10--resíduos-de-debug-e-mensagens-de-erro-sem-errno))
  - [ ] Sem código morto, sem TODOs sem dono. ← **falha hoje:** código comentado em `src/main.cpp:31-33`; 6 `TODO` no código (`CgiEnv.cpp`, `CgiHandler.cpp`, `PostHandler.cpp`, `Request.cpp`, `Router.cpp`, `SessionStore.cpp`) — todos com dono e tarefa correspondente, o que é aceitável até o épico fechar
  - [ ] `make`, `make clean`, `make fclean`, `make re` funcionam. ← **já verificado**
  - [ ] Checklist preenchido no PR final; cada membro assina o review.

---

## Resumo de tarefas

| ID | Tarefa | Owner | Status | Tamanho |
|----|--------|-------|--------|---------|
| E08-T01 | curl-suite expandido | M2 | ❌ (escopo ampliado) | L |
| E08-T02 | siege Availability ≥ 99.5% | M1 | ❌ | M |
| E08-T03 | Valgrind (leaks + FDs) | Todos | ❌ | M |
| E08-T04 | Browser testing | Todos | ❌ | S |
| E08-T05 | Edge cases | Todos | ❌ (escopo ampliado) | L |
| E08-T06 | Multi-server testing | M2 | ❌ | M |
| E08-T07 | Doc operacional | Todos | ❌ | S |
| E08-T08 | Code review final | Todos | ❌ | M |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`.

### BUG-08-01 — O `curl-suite` espera 403 onde o servidor responde 405

- **Origem:** E08-T01
- **Onde:** `tests/scripts/curl-suite.sh:31`
- **Sintoma:** o caso `check "DELETE forbid" "403" curl_status -X DELETE "http://$HOST/"`
  espera 403, mas `conf/default.conf` declara `location / { methods GET; }` — então o
  `Router` devolve **405 Method Not Allowed** com header `Allow: GET`, que é o comportamento
  correto. **O teste é que está errado, não o servidor.**
- **Esperado:** corrigir a expectativa para 405 e, se o cenário de 403 for desejado, criá-lo
  explicitamente (location com `methods DELETE` apontando para um arquivo sem permissão).
- **Severidade:** Baixa — mas um teste que falha por engano ensina o time a ignorar falhas,
  que é o pior hábito possível numa suíte.

### BUG-08-02 — O `curl-suite` não prepara nem limpa `www/uploads`

- **Origem:** E08-T01 (critério de idempotência)
- **Onde:** `tests/scripts/curl-suite.sh` (não há setup/teardown)
- **Sintoma:** o diretório `www/uploads` não está versionado
  ([BUG-05-02](epic-05-handlers-http.md#bug-05-02--wwwuploads-não-existe-no-repositório)),
  então num clone limpo qualquer teste de upload devolve 500. E, se o diretório existir, os
  arquivos de uma execução ficam para a seguinte — um teste de DELETE pode passar por causa
  do lixo da rodada anterior.
- **Esperado:** `mkdir -p www/uploads` no setup e limpeza do conteúdo antes e depois, mais o
  `.gitkeep` versionado.
- **Severidade:** Baixa — mas é pré-requisito de qualquer caso de POST/DELETE confiável.

### Nota — `make test` engole falhas

- **Onde:** `Makefile`, alvo `test`: `@bash tests/scripts/curl-suite.sh || true`
- **Sintoma:** o `|| true` garante que o `make test` **sempre** retorna 0, mesmo com testes
  falhando. Combinado com BUG-08-01 (um teste que falha por engano), o resultado é uma
  suíte decorativa.
- **Esperado:** remover o `|| true` assim que BUG-08-01 estiver corrigido, para que
  `make test` seja um sinal confiável.
- **Severidade:** Baixa — resolver junto de E08-T01.
