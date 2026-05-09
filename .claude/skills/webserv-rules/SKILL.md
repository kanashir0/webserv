---
name: webserv-rules
description: Audita o código contra as 5 restrições críticas do subject webserv da 42 (poll único, sem read/write fora de poll, fork só para CGI, single-thread, C++98 strict). Use sempre antes de abrir PR ou após adicionar I/O novo.
---

# webserv-rules — auditoria das restrições do subject

Você é um avaliador rigoroso do projeto webserv da 42 School. O subject reprova
o projeto inteiro se qualquer uma destas regras for violada. **Não negocie**.

## Restrições a verificar (todas são bloqueantes)

### 1. `poll()` em apenas um ponto

```bash
grep -rn '\bpoll\s*(' src/ include/
```

**Esperado:** uma única chamada real, dentro de `EventLoop::runOnce()` em
`src/core/EventLoop.cpp`. Includes (`#include <poll.h>`) e definições de
struct (`struct pollfd`) não contam. Qualquer outra chamada é violação.

Equivalentes proibidos: `select`, `epoll_wait`, `kqueue`, `kevent`.

```bash
grep -rEn '\b(select|epoll_wait|epoll_create|epoll_ctl|kqueue|kevent)\s*\(' src/ include/
```

### 2. Nunca `read`/`write`/`recv`/`send`/`accept` fora de callback de I/O

Toda chamada destas funções deve estar dentro de `onReadable()`,
`onWritable()`, `onHangup()` de um `IPollable`, ou em `accept()` chamado por
`ListeningSocket::onReadable()`.

```bash
grep -rEn '\b(read|write|recv|send|recvfrom|sendto|accept)\s*\(' src/
```

Para cada hit, abra o arquivo e confirme:
- Está dentro de método chamado pelo `EventLoop` após o `poll()` sinalizar?
- Ou é o `accept()` em `ListeningSocket::onReadable()`?

Se algum `read`/`write` aparece em construtor, destrutor, parser, handler ou
loop próprio: **violação**.

### 3. `fork()` apenas para CGI

```bash
grep -rEn '\b(fork|vfork|clone)\s*\(' src/ include/
```

**Esperado:** uma única chamada em `CgiHandler::start()` (ou método
equivalente em `src/cgi/`). Qualquer outra: violação.

### 4. Sem threads

```bash
grep -rEn '\b(pthread_|std::thread|std::async|std::mutex|std::condition_variable)\b' src/ include/
```

**Esperado:** zero hits. Se aparecer, falha imediata.

### 5. C++98 strict — proibido

```bash
grep -rEn '\b(nullptr|auto\s+[a-zA-Z]|std::to_string|std::shared_ptr|std::unique_ptr|std::make_shared|std::make_unique|constexpr|noexcept|override|final|emplace_back|std::move|std::forward)\b' src/ include/
grep -rEn 'for\s*\([^;]*:\s*' src/ include/      # range-for
grep -rEn '\{\s*[0-9].*\}' src/ include/         # uniform init suspeita
grep -rEn '\[\s*[a-zA-Z=&].*\]\s*\(' src/ include/  # lambdas
```

Também verifique:
- Sem `#include` de headers C++11+ (`<thread>`, `<memory>` com smart_ptr,
  `<unordered_map>`, `<chrono>`, `<atomic>`, `<functional>` para `std::function`).
- Sem `enum class` (use `enum` simples ou namespace).
- Sem `using` aliases (`using X = Y;`); usar `typedef`.

### 6. Compilação obrigatória

A flag exigida pelo subject:

```
c++ -Wall -Wextra -Werror -std=c++98 -pedantic -Iinclude
```

Confirme em `Makefile`:

```bash
grep -E 'CXXFLAGS|CFLAGS' Makefile
```

Se faltar `-Werror`, `-pedantic` ou `-std=c++98`: violação.

### 7. RAII de FDs

Nenhum `close()` manual fora de `~FileDescriptor()`:

```bash
grep -rn '\bclose\s*(' src/ | grep -v 'FileDescriptor.cpp'
```

Esperado: zero (ou apenas dentro do destrutor da própria classe).

## Saída esperada

Liste cada restrição como `✅ OK` ou `❌ VIOLAÇÃO em <arquivo>:<linha> — <motivo>`.
Não dê "parcialmente OK". Se houver qualquer violação, diga claramente:
**"Não abrir PR até corrigir."**
