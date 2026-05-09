# Skills do projeto webserv

Conjunto de skills compartilhadas pelos 3 membros do grupo. Use via `/<skill>`
no Claude Code, ou referencie pelo nome quando pedir ajuda ao agente.

## Como invocar

No prompt do Claude Code:

```
/webserv-rules        # checa as 5 restrições críticas do subject
/webserv-build        # compila com flags obrigatórias e zero warnings
/webserv-test         # roda curl-suite + siege + valgrind
/webserv-core         # ajuda M1 (EventLoop, Server, Client, Sockets, CGI exec)
/webserv-parsers      # ajuda M2 (ConfigParser, RequestParser, common/)
/webserv-http         # ajuda M3 (Router, handlers, ResponseFactory, sessions)
/webserv-cgi          # fluxo CGI ponta a ponta (M1+M2+M3)
/webserv-pr-checklist # verifica branch antes de abrir PR
```

## Mapa por dono

| Membro | Branch              | Skills primárias                          |
|--------|---------------------|-------------------------------------------|
| M1     | `feat/core-network` | `webserv-core`, `webserv-cgi`             |
| M2     | `feat/parsers`      | `webserv-parsers`, `webserv-cgi`          |
| M3     | `feat/http-logic`   | `webserv-http`, `webserv-cgi`             |
| Todos  | —                   | `webserv-rules`, `webserv-build`, `webserv-test`, `webserv-pr-checklist` |

## Antes de qualquer PR

Rode na ordem: `/webserv-rules` → `/webserv-build` → `/webserv-test` →
`/webserv-pr-checklist`. Só marque o PR como pronto quando todos passarem.

## Referências

- `CLAUDE.md` — visão geral da arquitetura e convenções
- `docs/architecture.md` — pilares + ownership por módulo
- `docs/git-workflow.md` — branches, commits e PRs
- `docs/testing.md` — bateria de testes
- `docs/ideia_inicial.md` — plano original do grupo
