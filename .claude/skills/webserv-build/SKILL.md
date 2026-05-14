---
name: webserv-build
description: Compila o webserv com as flags obrigatórias do subject (-Wall -Wextra -Werror -std=c++98 -pedantic) e garante zero warnings. Use após alterações de código e antes de push.
---

# webserv-build — compilação validada

## Procedimento

```bash
make fclean
make 2>&1 | tee /tmp/webserv-build.log
```

## Critérios de sucesso

1. **Exit code 0** do `make`.
2. **Zero linhas com `warning:`** em `/tmp/webserv-build.log`:
   ```bash
   grep -c 'warning:' /tmp/webserv-build.log   # deve ser 0
   ```
3. **Zero `error:`** (óbvio, mas confirme):
   ```bash
   grep -c 'error:' /tmp/webserv-build.log     # deve ser 0
   ```
4. **Binário produzido**:
   ```bash
   test -x ./webserv && ./webserv --help 2>/dev/null; echo "exit=$?"
   ```

## Se houver warning

- **Não suprima com `#pragma`, `__attribute__((unused))` ou cast de void.**
- Para parâmetros não usados em stubs, comente o nome:
  `void foo(int /* fd */) {}` — já é convenção do projeto (ver CLAUDE.md).
- Para variáveis temporariamente não usadas em desenvolvimento, prefira
  remover; nunca silenciar.

## Verificação extra de flags

```bash
grep -E '^(CXX|CXXFLAGS)' Makefile
```

Deve conter literal: `-Wall -Wextra -Werror -std=c++98 -pedantic`.
Se algum membro removeu uma flag temporariamente, restaure antes do PR.

## Modo ainda mais estrito (recomendado pré-PR)

```bash
make fclean && make CXXFLAGS_EXTRA="-Wshadow -Wconversion -Wold-style-cast"
```

Esses warnings extras não são exigidos pelo subject mas pegam bugs sutis
(shadowing de membros, conversões implícitas perigosas, casts estilo C).
Não precisam estar zerados, mas vale revisar.

## Relatório

Ao terminar, reporte exatamente:

```
Build: OK | warnings: 0 | binário: ./webserv (XXX KB)
```

Se falhou, mostre as 10 primeiras linhas relevantes do log.
