# Git Workflow

## Branches

- `main` — protegida. Sem push direto. Só merge via PR.
- `feat/<modulo>` — uma por membro:
  - `feat/core-network`  (Membro 1)
  - `feat/parsers`       (Membro 2)
  - `feat/http-logic`    (Membro 3)
- `fix/<descricao>`, `chore/<descricao>` para mudanças menores.

## Pull Requests

1. PR pequeno (idealmente <500 linhas de diff).
2. Descrição com **o que** e **como testar**.
3. Marcar reviewer (pelo menos 1 dos outros 2 membros).
4. Após aprovação, **squash merge** para manter `main` linear.

## Commits

- Inglês, imperativo: `add RequestParser chunked body support`.
- Pequenos e por unidade lógica.

## Checklist antes de marcar PR como pronto

- `make re` compila sem warning (`-Wall -Wextra -Werror`).
- Sem warnings de `valgrind` em uso normal.
- Testes do módulo passam (`make test` ou script específico).
- Sem código morto, sem `printf` de debug.

## Tags

- `v0.1` — mandatory completo.
- `v1.0` — bônus (cookies/sessions, múltiplos CGI).
