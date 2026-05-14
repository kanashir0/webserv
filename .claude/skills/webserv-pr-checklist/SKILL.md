---
name: webserv-pr-checklist
description: Checklist final antes de marcar um PR como pronto para review (branch correta, tamanho, build, testes, sem código morto, descrição com como-testar). Use sempre antes de pedir review aos outros membros.
---

# webserv-pr-checklist — antes de marcar PR como pronto

Rode este checklist na branch da feature (não em `main`).

## 1. Branch e escopo

```bash
git branch --show-current     # deve ser feat/<modulo>, fix/* ou chore/*
git log --oneline main..HEAD  # commits do PR
git diff --stat main..HEAD    # tamanho do diff
```

- Branch segue convenção: `feat/core-network`, `feat/parsers`, `feat/http-logic`,
  `fix/<descricao>` ou `chore/<descricao>`.
- Diff < **500 linhas** (idealmente). Se maior, considere quebrar em PRs menores.
- Commits pequenos, mensagem em **inglês imperativo** (`add`, `fix`, `refactor`),
  não `added`/`fixed`.

## 2. Build limpo

```bash
make fclean && make 2>&1 | tee /tmp/b.log
grep -c warning: /tmp/b.log    # esperado: 0
```

Veja `/webserv-build` para detalhes.

## 3. Restrições do subject

Rode `/webserv-rules` — todas as 7 verificações precisam estar `OK`.

Mais importantes:
- 1 só `poll()` (em `EventLoop::runOnce()`)
- 1 só `fork()` (em `CgiHandler::start()`)
- Nenhum `pthread_*` ou `std::thread`
- C++98 (sem `nullptr`, `auto`, `std::to_string`, range-for)

## 4. Testes

Rode `/webserv-test` — pelo menos os 3 primeiros níveis (smoke + edge cases +
siege). Valgrind pelo menos uma vez por PR.

## 5. Higiene de código

```bash
# Nenhum printf/std::cout/cerr de debug deixado para trás
grep -rEn 'std::c(out|err)|printf\(' src/ | grep -vE 'Logger|main\.cpp'

# Nenhum TODO/FIXME novo sem issue associada
git diff main..HEAD | grep -E '^\+.*\b(TODO|FIXME|XXX)\b'

# Nenhum arquivo binário/objeto commitado
git diff --stat main..HEAD | grep -E '\.(o|a|out)$|^build/'
```

Os 3 devem retornar vazio.

## 6. Descrição do PR

Template mínimo:

```markdown
## O que
<1-3 frases descrevendo a mudança>

## Por que
<contexto: qual issue/módulo, qual restrição do subject>

## Como testar
```bash
make re
./webserv conf/default.conf
# (comandos curl/nc específicos do que mudou)
```

## Checklist
- [ ] make compila sem warning (`-Wall -Wextra -Werror`)
- [ ] valgrind sem leaks
- [ ] testes do módulo passam
- [ ] reviewer marcado: @<um dos outros 2 membros>
```

## 7. Sincronia com main

```bash
git fetch origin
git log --oneline HEAD..origin/main      # commits novos em main
```

Se `main` avançou, faça **rebase** (não merge):

```bash
git rebase origin/main
# resolva conflitos, depois:
make re && /webserv-test           # garanta que ainda passa
git push --force-with-lease
```

## 8. Squash merge

A `main` é protegida — só aceita squash merge via PR aprovado por outro
membro. **Não** tente push direto. Se não tem permissão para mergear, peça
ao membro que aprovou.

## Tags

- `v0.1` — quando mandatory completo (todos os 7 edge cases passam +
  siege ≥ 99.5 % + valgrind limpo).
- `v1.0` — bônus (cookies/sessions, múltiplos CGI).

Quem cria a tag: feita em consenso na reunião final, não unilateralmente.

## Se algo falhou

Reporte ao usuário a lista exata de itens que falharam, na ordem do checklist,
e diga: **"Não marcar PR como pronto até corrigir os itens X, Y, Z."**
