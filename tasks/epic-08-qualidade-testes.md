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

---

## E08-T01 — Estender `tests/scripts/curl-suite.sh` (smoke completo)

- **Owner:** M2 (mantenedor da suite)
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/curl-suite.sh`
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** ampliar a suite de smoke tests com asserts automatizados (status code esperado, presença/ausência de header, conteúdo do body via `grep`). Cobrir todos os cenários de `docs/testing.md`.
- **Critérios de aceite:**
  - [ ] Cobertura mínima: GET 200, GET 404, GET 403 (autoindex off), POST 201, DELETE 204, PUT 405, body 413, URI 414, body chunked, redirect 301/302, virtual hosting (`-H Host:`).
  - [ ] Cada teste imprime `[PASS]` ou `[FAIL]` colorido.
  - [ ] Script retorna exit code 0 se tudo passa, ≠ 0 se algum falha.
  - [ ] Pode ser invocado via `make test`.
  - [ ] Idempotente: limpa `www/uploads/*` antes/depois de tests de upload/delete.

---

## E08-T02 — Stress test com siege (Availability ≥ 99.5%)

- **Owner:** M1 (dono do motor de rede)
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/run-siege.sh`
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** rodar `siege -c50 -t30s http://localhost:8080/` e validar que Availability ≥ 99.5% e que o servidor continua atendendo após o teste. Ajustar `tests/scripts/run-siege.sh` para parsear o output e falhar se a métrica não for atingida.
- **Critérios de aceite:**
  - [ ] `siege -c50 -t30s` reporta Availability ≥ 99.5%.
  - [ ] Servidor continua atendendo após o stress (`curl http://localhost:8080/` retorna 200).
  - [ ] Sem zombies, sem FDs vazados (`lsof -p <pid>` antes/depois com diff ≤ 5).
  - [ ] Versão para CGI: `siege -c20 -t10s http://localhost:8080/cgi-bin/env_dump.py` sem queda.

---

## E08-T03 — Validação de memory leaks e FD leaks com Valgrind

- **Owner:** Todos (cada um valida sua área)
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/run-valgrind.sh`
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** rodar o servidor sob `valgrind --leak-check=full --track-fds=yes --error-exitcode=1`. Submeter cargas representativas (curl-suite + algumas conexões CGI). Verificar que `definitely lost: 0 bytes` e `Open file descriptors:` retorna apenas FDs do `valgrind` em si.
- **Critérios de aceite:**
  - [ ] `definitely lost: 0 bytes`, `indirectly lost: 0 bytes`.
  - [ ] `still reachable` é tolerado se for de bibliotecas do sistema (libstdc++).
  - [ ] FDs abertos no fim: apenas 0, 1, 2 (stdin/stdout/stderr).
  - [ ] Script falha com exit ≠ 0 se houver leak.
  - [ ] Roda sob ambos os modos: shutdown gracioso (SIGINT) e shutdown abrupto (kill -9 não conta para FDs do servidor).

---

## E08-T04 — Testes de compatibilidade com browsers

- **Owner:** Todos (manual)
- **Tamanho:** S
- **Arquivos afetados:** documentação de teste manual (`tests/manual-browser.md` novo)
- **Dependências:** Épicos 01–05 funcionais
- **Descrição:** abrir `http://localhost:8080/` em Chrome, Firefox, Safari (se disponível). Verificar:
  - Página index renderiza.
  - DevTools (`F12 → Network`) sem requests com erro.
  - Headers HTTP corretos (`Content-Type`, `Content-Length`, `Date`).
  - Upload via formulário multipart funciona.
  - Cookies (se Épico 07 implementado) persistem entre reloads.
- **Critérios de aceite:**
  - [ ] Documento `tests/manual-browser.md` lista o passo a passo.
  - [ ] Screenshots ou logs anexados (opcional).
  - [ ] Cada membro do time valida ao menos 1 browser.

---

## E08-T05 — Bateria de edge cases manuais

- **Owner:** Todos
- **Tamanho:** M
- **Arquivos afetados:** `tests/scripts/test-edge-cases.sh` (novo)
- **Dependências:** Épicos 01–05
- **Descrição:** automatizar os edge cases listados em `docs/testing.md` e expandir:
  - Body > `clientMaxBodySize` → 413.
  - URI > 8192 chars → 414.
  - POST sem `Content-Length` nem `Transfer-Encoding` → 411.
  - `HTTP/2.0` → 505.
  - `Transfer-Encoding: chunked` → 200.
  - Header com `\0` → 400.
  - Path traversal (`/../etc/passwd`) → 400/403, nunca 200.
  - Conexão fechada antes de enviar request inteira → servidor não trava.
  - 100 clientes simultâneos enviando 1 byte/segundo → todos atendidos sem timeout precoce.
- **Critérios de aceite:**
  - [ ] Todos os cenários acima cobertos no script.
  - [ ] Cada caso tem resultado esperado documentado.
  - [ ] Roda sob `make test` ou `bash tests/scripts/test-edge-cases.sh`.

---

## E08-T06 — Testes de configurações múltiplas

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `tests/configs/multi-server.conf`, `tests/scripts/test-multi-server.sh` (novo)
- **Dependências:** Épico 01, Épico 02
- **Descrição:** validar virtual hosting:
  - Duas configs com `listen 8080` mas `server_name` diferentes.
  - `curl -H "Host: site-a.local"` vs `curl -H "Host: site-b.local"` → conteúdos diferentes.
  - Mesma porta com vários servers funciona (1 listening socket compartilhado).
  - Hosts diferentes com portas diferentes (8080 e 9090) funcionam isolados.
- **Critérios de aceite:**
  - [ ] Servidor inicia com sucesso usando `tests/configs/multi-server.conf`.
  - [ ] Asserts via `grep` no body verificam que o vhost correto respondeu.
  - [ ] Host desconhecido cai no vhost default (primeiro declarado).

---

## E08-T07 — Documentação operacional final

- **Owner:** Todos (rotação)
- **Tamanho:** S
- **Arquivos afetados:** `README.md` (atualização), `docs/`, `CLAUDE.md`
- **Dependências:** todos os épicos
- **Descrição:** garantir que README e docs refletem o estado real do projeto: comandos, exemplos de configuração, troubleshooting. Adicionar seção "Como rodar" e "Como testar" no README. Atualizar tabela de ownership (marcar tarefas concluídas).
- **Critérios de aceite:**
  - [ ] README com seção "Quick Start" funcional (copy-paste de comandos roda).
  - [ ] Exemplos de `.conf` documentados.
  - [ ] Troubleshooting comum (porta em uso, sem permissão, valgrind reports).
  - [ ] Tag `v0.1` criada quando mandatory pronto; `v1.0` quando bônus pronto.

---

## E08-T08 — Code review final + checklist do subject 42

- **Owner:** Todos (cross-review)
- **Tamanho:** M
- **Arquivos afetados:** todos
- **Dependências:** Épicos 01–07 completos
- **Descrição:** auditoria final antes do defense. Cada membro revisa o código dos outros 2 procurando:
  - Apenas 1 `poll()` em todo o projeto (`grep -rn 'poll(' src/`).
  - Nenhum `read()`/`write()`/`recv()`/`send()` fora dos callbacks de `IPollable`.
  - Apenas 1 `fork()` (em `CgiHandler::start`).
  - Sem `pthread_*`, sem `std::thread`.
  - Compila com `c++ -Wall -Wextra -Werror -std=c++98 -pedantic` sem warnings.
  - Sem `printf` de debug, sem `std::cout` de debug.
  - Sem código morto, sem TODOs sem dono.
  - `make`, `make clean`, `make fclean`, `make re` funcionam.
- **Critérios de aceite:**
  - [ ] Checklist preenchido em PR final (todos os ítens marcados).
  - [ ] Cada membro assina o review com comentário no PR.
  - [ ] Build limpa (sem warnings) na CI/CD ou manualmente.
  - [ ] `make re` em ambiente clean compila do zero sem erro.

---

## Resumo de tarefas

| ID | Tarefa | Owner | Tamanho |
|----|--------|-------|---------|
| E08-T01 | curl-suite expandido | M2 | M |
| E08-T02 | siege Availability ≥ 99.5% | M1 | M |
| E08-T03 | Valgrind (leaks + FDs) | Todos | M |
| E08-T04 | Browser testing | Todos | S |
| E08-T05 | Edge cases manuais | Todos | M |
| E08-T06 | Multi-server testing | M2 | M |
| E08-T07 | Doc operacional | Todos | S |
| E08-T08 | Code review final | Todos | M |
