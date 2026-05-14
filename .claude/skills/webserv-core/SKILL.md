---
name: webserv-core
description: Apoio ao Membro 1 (branch feat/core-network) na implementação de EventLoop, Server, ListeningSocket, Client e parte de execução do CgiHandler. Use ao escrever ou revisar código em src/core/ ou src/cgi/ relacionado a I/O.
---

# webserv-core — guia do Membro 1

Você está editando o "motor" do servidor: tudo que toca FDs, `poll()`, accept,
recv/send e o ciclo de vida das conexões.

## Princípios não-negociáveis

1. **`poll()` só em `EventLoop::runOnce()`.** Nenhum outro arquivo chama poll.
2. **Nunca `recv`/`send`/`accept` sem o `poll()` ter retornado o evento.**
   Use sempre os callbacks `onReadable()`/`onWritable()`/`onHangup()`.
3. **`O_NONBLOCK` em todo socket** (listening e accept), aplicado via
   `fcntl(fd, F_SETFL, O_NONBLOCK)` no momento da criação.
4. **RAII via `FileDescriptor`.** Nunca chamar `close()` direto. Para
   transferir ownership use `release()`.
5. **Single-thread.** Nada de `pthread`, `std::thread`, `std::async`.
6. **Iterator invalidation:** nunca delete um `IPollable` dentro de seu
   próprio callback. Marque `wantsClose_ = true` e deixe `reapClosed()`
   limpar depois do loop.

## Interface `IPollable`

Toda entidade registrada no loop implementa:

```cpp
class IPollable {
public:
    virtual ~IPollable() {}
    virtual int   fd() const = 0;
    virtual short interest() const = 0;       // POLLIN | POLLOUT
    virtual void  onReadable() = 0;
    virtual void  onWritable() = 0;
    virtual void  onHangup()   = 0;
    virtual bool  wantsClose() const = 0;
};
```

`interest()` é consultado a cada iteração do loop — mudar de `POLLIN` para
`POLLOUT` quando o `Client` transitar para `WRITING_RESPONSE`.

## State machine do `Client`

```
READING_HEADERS → READING_BODY → ROUTING → WRITING_RESPONSE → DONE
```

- `interest()` retorna `POLLIN` em READING_*, `POLLOUT` em WRITING_RESPONSE.
- Em `DONE`, marca `wantsClose_ = true`. O reaper do loop deleta na próxima
  iteração.
- `recv()` retornando 0 → cliente fechou: `onHangup()` → marca close.
- `recv()`/`send()` retornando -1: **NÃO** cheque `errno` (`EAGAIN`/`EWOULDBLOCK`)
  — o subject proíbe. Apenas trate como "tente de novo no próximo poll".

## Envio parcial (importantíssimo)

`send()` pode enviar menos bytes do que pedido. Mantenha um offset:

```cpp
ssize_t n = send(fd_, outBuf_.data() + outOffset_, outBuf_.size() - outOffset_, 0);
if (n > 0) outOffset_ += n;
if (outOffset_ == outBuf_.size()) state_ = DONE;
```

O mesmo vale para `recv()`: acumule em buffer até o parser dizer COMPLETE.

## CGI (parte do M1)

`CgiHandler` é um `IPollable` que envolve dois pipes (stdin do filho, stdout
do filho) e um `pid_t`. Fluxo:

1. `pipe(p_in)`, `pipe(p_out)` — marque os 4 FDs como `O_NONBLOCK`.
2. `fork()` (esta é a **única** fork permitida no projeto).
3. Filho: `dup2(p_in[0], 0)`, `dup2(p_out[1], 1)`, fecha duplicatas, `execve()`.
4. Pai: fecha pontas do filho, registra os FDs do pai no `EventLoop`.
5. `waitpid(pid, &status, WNOHANG)` periodicamente — nunca bloqueante.
6. Timeout: se passou X segundos, `kill(pid, SIGKILL)` + `waitpid` para reap.

**Não esqueça** de fechar todos os FDs herdados pelo filho que não são os 3
padrão — pipes de outros clientes vazariam para dentro do CGI.

## Checklist de PR (M1)

- [ ] `grep -c '\bpoll\s*(' src/` retorna 1
- [ ] `grep -c '\bfork\s*(' src/` retorna 1
- [ ] Todo `recv`/`send`/`accept` está dentro de callback de `IPollable`
- [ ] Todo socket criado tem `O_NONBLOCK` aplicado antes do uso
- [ ] Nenhum `close()` manual fora de `~FileDescriptor`
- [ ] Valgrind reporta 0 FDs vazados após shutdown
- [ ] Siege com 50 conexões 30s mantém availability ≥ 99.5 %
