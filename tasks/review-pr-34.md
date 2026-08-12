# Revisão do PR #34 — `feat/core-network` → `main`

> **PR:** https://github.com/kanashir0/webserv/pull/34
> **Autor:** Caio Santos · **Revisor:** Gabriel Kanashiro
> **Data:** 11/08/2026
> **Commits:** `9e65fa7` (refactor: Bugs e ajustes abertos) + `ff09109` (refactor: bugs resolvidos)
> **Escopo:** 7 arquivos, +164 / −148
> **Veredito:** 🔴 **request changes — o PR não compila.**

---

## TL;DR

O PR ataca corretamente 6 dos 10 bugs abertos do [Épico 01](epic-01-motor-de-rede.md) e a
refatoração do `Server::start` (agrupamento por endpoint) e do ownership dos `IPollable`
está boa. Mas **o código não passa no `make`**: dois erros de compilação, um deles um
`nullptr` em C++98. Além disso, o fix de pipelining tem um furo que trava a conexão, e o
`recv()` continua sem fechar a conexão em erro.

Um bug do épico (BUG-01-01) foi **invalidado**: ele mandava usar uma flag proibida pelo
subject. O código atual está certo e não deve ser tocado. Detalhes na seção final.

---

## 🔴 1. Bloqueadores — o PR não compila

Rodando `make re` na branch:

```
c++ -Wall -Wextra -Werror -std=c++98 -pedantic -Iinclude -MMD -MP -c src/core/Client.cpp
src/core/Client.cpp:155:57: error: identifier 'nullptr' is a keyword in C++11 [-Werror=c++11-compat]
src/core/Client.cpp:155:57: error: 'nullptr' was not declared in this scope
```

```
c++ ... src/core/Server.cpp
include/core/Server.hpp:18:37: error: conflicting declaration 'typedef struct std::pair<std::string, int> Endpoint'
include/core/Server.hpp:13:8:  note: previous declaration as 'struct Endpoint'
src/core/Server.cpp:73:64: error: no matching function for call to 'Endpoint::Endpoint(std::string&, int&)'
src/core/Server.cpp:81:75: error: 'const struct Endpoint' has no member named 'first'
```

### 1.1 `nullptr` em C++98 — `src/core/Client.cpp:155`

```cpp
RequestParser::FeedResult result = parser_.feed(nullptr, 0, matchVirtualHost().clientMaxBodySize);
//                                              ^^^^^^^ C++11
```

**Correção:** trocar por `NULL`.

### 1.2 `Endpoint` declarada duas vezes — `include/core/Server.hpp:13-18`

```cpp
struct Endpoint {              // ← sobra de uma tentativa abandonada
	std::string host;
	int         port;
};

typedef std::pair<std::string, int> Endpoint;   // ← o que o Server.cpp realmente usa
```

Em C++ um nome de classe e um `typedef` para outro tipo não podem coexistir. O
`Server::start()` foi escrito para o `typedef` (usa `it->first.first` / `it->first.second`).

**Correção:** apagar o `struct Endpoint` inteiro e ficar só com o `typedef`.

> **Nota de processo:** os dois commits nunca passaram por `make`. Vale rodar
> `make re` antes de abrir PR — a skill `webserv-build` existe exatamente para isso.

---

## ✅ 2. O que foi corrigido corretamente

| Bug | Onde | O que ficou bom |
|---|---|---|
| **BUG-01-02** — accept lançava exceção | `src/common/Socket.cpp:44` | `throw` virou `LOG_WARN` + `return -1`. O `ECONNABORTED` de rotina não derruba mais o processo. |
| **BUG-01-03** — `Client` criado com fd -1 | `src/core/Server.cpp:38-39` | `break` agora acontece em **todo** retorno negativo, não só no `EAGAIN`. Fecha o risco de loop infinito. |
| **BUG-01-04** — agrupamento de vhosts | `src/core/Server.cpp:71-91` | Reescrito: agrupa em `std::map<Endpoint, std::vector<ServerConfig> >` **antes** de criar qualquer socket, e cria exatamente um `ListeningSocket` por chave. O `else break` e o bind redundante sumiram. O `std::cout` de debug e o `try/catch` que engolia a falha também — agora a exceção sobe até o `catch` do `main`, que retorna 1. Exatamente o que a tarefa pedia. |
| **BUG-01-05** — `Host` com porta | `src/core/Client.cpp:127-137` | `substr(0, find(":"))` + `StringUtils::iequals`. Virtual hosting passa a funcionar com `curl`/browser de verdade. |
| **BUG-01-07** — leak no shutdown | `src/core/EventLoop.cpp:4-10` | `~EventLoop` deleta os `IPollable*` restantes e `~Server` foi esvaziado. Ownership unificado no `EventLoop` — é a decisão que o épico pedia para tomar e documentar. |
| **BUG-01-08.1** — estados mortos | `include/core/Client.hpp:24-28` | `READING_BODY` e `ROUTING` removidos do enum e do `switch` do `interest()`. Escolha certa: a máquina de estados real é a do `RequestParser`. |

**Parcialmente resolvidos:**

- **BUG-01-06** (timeout ms×s) — `EventLoop.cpp:52` passa `60` literal em vez de `timeoutMs`.
  Funciona, mas o critério de aceite "*o valor de timeout é configurável (default 60s)*"
  continua aberto. Sugestão: membro `timeoutSec_` no `Server`, injetado no `EventLoop`.
- **BUG-01-10** (resíduos de debug) — `strerror(errno)` entrou no `Socket.cpp` e o
  `LOG_WARN` do startup virou `LOG_INFO`. Mas `src/main.cpp:29-33` continua com o log
  `"webserv starting (skeleton, no real I/O yet)"` (desatualizado) e três linhas de código
  comentado logo abaixo. E08-T08 audita isso.

---

## 🟠 3. Defeitos introduzidos pelo PR

### 3.1 `tryConsumeResidual` engole erro de parsing — `src/core/Client.cpp:154-163`

```cpp
bool Client::tryConsumeResidual() {
	RequestParser::FeedResult result = parser_.feed(nullptr, 0, matchVirtualHost().clientMaxBodySize);
	if (result == RequestParser::COMPLETE) {
		...
		return false;
	}
	return true;   // ← NEED_MORE **e** BAD_REQUEST/URI_TOO_LONG/BODY_TOO_LARGE caem aqui
}
```

A função só trata `COMPLETE`. Se a segunda request do pipelining for inválida, o parser
entra em `state_ == ERROR`, a função devolve `true`, o `onWritable` faz
`state_ = READING_HEADERS` e o cliente fica esperando bytes que não resolvem nada —
`RequestParser::feed` tem `if (state_ == ERROR) return errorToResult(...)` logo na primeira
linha, então **toda** chamada seguinte devolve erro. Resultado: conexão travada até o
timeout, sem nunca mandar o 400.

**Correção:** replicar o `else` que o `onReadable` já tem — `buildErrorResponse(parser_.errorStatus())`,
`closeAfterWrite_ = true`, `state_ = WRITING_RESPONSE`, `return false`.

### 3.2 `recv() < 0` não fecha a conexão — `src/core/Client.cpp:58-59`

```cpp
if (ret < 0)
	return;
```

Um `ECONNRESET` deixa a conexão presa até o timeout. **Atenção:** o épico (BUG-01-08.2)
pede para "distinguir `EAGAIN` de erro real" olhando `errno` — **isso está errado e não
deve ser feito.** O subject proíbe checar `errno` após `read`/`write` (ver
`README.md`, tabela de restrições). A correção correta é fechar em qualquer retorno negativo:

```cpp
if (ret < 0) {
	wantsClose_ = true;
	state_ = DONE;
	return;
}
```

Nota: a remoção do check de `EAGAIN` no `onWritable` (`Client.cpp:89`) que este PR fez segue
exatamente essa lógica — está **certa**. Falta só aplicar a mesma no `onReadable`.

### 3.3 `if (!listener) throw` — `src/core/Server.cpp:82-83`

Código morto: `new` lança `std::bad_alloc` em falha, nunca devolve `NULL`. E a mensagem
tem typo (`"SOCKER FAIL"`). **Correção:** apagar as duas linhas.

### 3.4 `groups_` virou membro de `Server` — `include/core/Server.hpp:70`

É usado exclusivamente dentro de `start()`. Como membro, mantém uma cópia completa de todas
as `ServerConfig` viva pelo processo inteiro, duplicando o que já está em `configs_`.
**Correção:** declarar como variável local em `start()` e remover do header.

### 3.5 Código morto novo — `src/core/Server.cpp:114-124`

Depois da refatoração do `start()`, `addServer()`, `getHost()` e `getPort()` não têm mais
nenhum chamador. **Correção:** remover os três de `Server.cpp` e de `Server.hpp` (E08-T08).

### 3.6 Ordem de destruição perigosa em `Server` — `include/core/Server.hpp:66-72`

Ordem atual dos membros: `configs_`, `groups_`, `loop_`, `listeners_`, `sessions_`,
`router_`. A destruição é na ordem inversa, ou seja: `sessions_` é destruído **antes** de
`loop_` deletar os `Client`s — e cada `Client` guarda um `SessionStore&`.

Hoje é inócuo porque `~Client()` é vazio, mas é uma armadilha esperando alguém colocar
lógica no destrutor. **Correção:** mover `loop_` para o **fim** da lista de membros, para
que ele seja destruído primeiro.

### 3.7 Log duplicado no accept

`Socket::acceptConnection` loga `WARN "ACCEPT FAIL!"` e, no mesmo evento,
`ListeningSocket::onReadable` loga `ERROR "ACCEPT CLIENT FAIL"`. Nenhuma das duas inclui
`strerror(errno)` — que era justamente o ponto do BUG-01-10. **Correção:** manter um só log,
no `Socket`, com `strerror(errno)`.

### 3.8 `<map>` não incluído — `include/core/Server.hpp`

O header declara `std::map` como membro mas inclui só `<vector>`. Compila por include
transitivo; é frágil. **Correção:** adicionar `#include <map>`.

---

## ⚪ 4. Ruído no diff

`include/core/Client.hpp` e `include/core/Server.hpp` foram **inteiramente re-indentados**
(os `public:`/`private:` recuados um nível, e todo o corpo junto). Isso responde por cerca
de 150 das ~310 linhas do diff e esconde as mudanças reais — o `tryConsumeResidual`, o
`groups_`, o enum enxuto.

**Pedido:** reverter a re-indentação e deixar no diff só as linhas de conteúdo. Facilita
esta revisão e as futuras (`git log -p` fica legível). Se o time quiser padronizar
indentação de headers, que seja num commit separado, só de formatação.

---

## 🔵 5. BUG-01-01 foi INVALIDADO — não corrija `setNonBlocking`

Esta é a parte mais importante do relatório, porque afeta documentação que estava mandando
o time escrever código que **reprova na defesa**.

### O que o subject diz

> No entanto, você só pode usar `fcntl()` com as seguintes flags:
> **`F_SETFL`, `O_NONBLOCK` e `FD_CLOEXEC`.**
> Qualquer outra flag é proibida.

`F_GETFL` **não** está nessa lista.

### O que o épico estava mandando fazer

O BUG-01-01 pedia trocar o código atual por:

```cpp
fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);   // ← VIOLA O SUBJECT
```

Ou seja: o "bug" mandava introduzir uma violação. Quem tivesse "corrigido" teria piorado o
projeto. **O código atual (`src/common/Socket.cpp:55`) está certo:**

```cpp
fcntl(server_fd, F_SETFL, O_NONBLOCK);   // ← correto, mantenha assim
```

O PR ter deixado isso intacto passa de omissão a acerto.

### O receio técnico também não procedia

O épico justificava a severidade com *"vira problema real assim que os pipes do CGI
(E06-T03) passarem por aqui"*. Não vira:

- `F_SETFL` só altera cinco flags: `O_APPEND`, `O_ASYNC`, `O_DIRECT`, `O_NOATIME` e `O_NONBLOCK`.
- Os bits de modo de acesso (`O_RDONLY`/`O_WRONLY`) são **ignorados** por `F_SETFL`, não apagados.
- Um FD recém-saído de `socket()` ou `pipe()` não tem nenhuma dessas cinco flags setada.

Logo, não há nada a preservar. `F_GETFL` seria custo puro mesmo se fosse permitido.

### Documentação já corrigida

A restrição não estava registrada em lugar nenhum do repositório — foi essa ausência que
produziu o bug fantasma. Já ajustei, nesta mesma sessão:

| Arquivo | Mudança |
|---|---|
| `CLAUDE.md` | nova linha na tabela "Restrições críticas do subject" |
| `README.md:447` | `setNonBlocking` documentado com a forma correta + aviso sobre `F_GETFL` |
| `README.md` (tabela de conformidade) | nova linha de restrição |
| `.claude/skills/webserv-core/SKILL.md:15` | princípio 3 agora explicita a proibição |
| `.claude/skills/webserv-cgi/SKILL.md:79-80` | comentários nos `fcntl` dos pipes |
| `tasks/epic-01-motor-de-rede.md` | E01-T02 → ✅, BUG-01-01 → ❌ INVALIDADO, tabela-resumo e contagem do épico atualizadas |

**Contagem do Épico 01: de `5 ✅ / 8 ⚠️` para `6 ✅ / 7 ⚠️`.**

---

## 🔵 6. `errno` após I/O — mesma história, mesma documentação errada

O BUG-01-01 não foi o único caso de documentação mandando violar o subject. O **BUG-01-08.2**
(§3.2 acima) pedia:

> *"fechar a conexão quando `errno` não for `EAGAIN/EWOULDBLOCK`"*

O subject proíbe consultar `errno` após `read`/`recv`/`write`/`send`. **Não há exceção, nem
mesmo para `EAGAIN`.** Seguir esse critério de aceite reprovaria na defesa exatamente como
o `F_GETFL`.

### A regra prática

| Syscall | `errno` permitido? | O que fazer em `-1` |
|---|---|---|
| `recv` / `send` / `read` / `write` | ❌ **proibido** | fechar: `wantsClose_ = true`, `state_ = DONE` |
| `accept` | ⚠️ não é proibido explicitamente, mas evite | sair do loop de accept |
| `socket` / `bind` / `listen` / `fcntl` | ✅ permitido | `throw std::runtime_error(strerror(errno))` — E01-T01 **exige** |

A perda de informação é real e aceita: não dá para distinguir "sem dados agora" de
`ECONNRESET`. É seguro porque `recv`/`send` só são chamados dentro de
`onReadable()`/`onWritable()`, ou seja, só depois de o `poll()` ter sinalizado
`POLLIN`/`POLLOUT` naquele FD — o `EAGAIN` espúrio é raro o bastante para que fechar a
conexão seja o custo aceitável.

**Reforçando:** o PR #34 removeu o check de `EAGAIN` do `onWritable` (`Client.cpp:89`). Isso
está **certo** e não deve ser revertido, mesmo que o épico dissesse o contrário. Falta só
aplicar a mesma regra no `onReadable` — que hoje faz `return` em vez de fechar.

### Documentação já corrigida

| Arquivo | Mudança |
|---|---|
| `CLAUDE.md` | nova linha na tabela de restrições críticas |
| `README.md` (`onReadable`/`onWritable`) | passos 3 e 4 reescritos + bloco de aviso explicando a regra e a distinção I/O × `socket`/`bind`/`listen`/`fcntl` |
| `README.md` (tabela de conformidade) | linha detalhada substituindo o "`errno` só antes do próximo syscall", que era ambíguo |
| `.claude/skills/webserv-core/SKILL.md` | armadilha 1 agora diz o que fazer no `-1` |
| `.claude/skills/webserv-rules/SKILL.md` | **novas seções 8 e 9** com os `grep` de auditoria para `errno` e para as flags do `fcntl` |
| `tasks/epic-01-motor-de-rede.md` | E01-T03, E01-T07, E01-T09, E01-T10 e BUG-01-08 reescritos; BUG-01-03 ganhou nota para remover o `if (errno == EAGAIN)` |
| `tasks/epic-06-cgi.md` | critério dos pipes do CGI (E06-T04) reescrito |
| `tasks/PLANNING.md` | linha do BUG-01-08 na tabela de prioridades |

---

## 7. Checklist para desbloquear o merge

Mínimo obrigatório:

- [ ] `src/core/Client.cpp:155` — `nullptr` → `NULL`
- [ ] `include/core/Server.hpp:13-17` — apagar o `struct Endpoint`
- [ ] `make re` passa limpo, zero warnings
- [ ] `src/core/Client.cpp:154-163` — tratar o ramo de erro no `tryConsumeResidual`
- [ ] `src/core/Client.cpp:58` — fechar a conexão em `recv() < 0` (sem olhar `errno`)

Recomendado no mesmo PR (é tudo remoção, diff curto):

- [ ] apagar `if (!listener) throw` (`Server.cpp:82`)
- [ ] apagar `addServer` / `getHost` / `getPort` (`Server.cpp:114-124` + header)
- [ ] `groups_` → variável local em `start()`
- [ ] mover `loop_` para o fim dos membros de `Server`
- [ ] `#include <map>` em `Server.hpp`
- [ ] limpar `main.cpp:29-33` (log desatualizado + código comentado)
- [ ] reverter a re-indentação dos dois headers

- [ ] `src/core/Server.cpp:37` — remover o `if (errno == EAGAIN)`: o `break` vale para todo `-1` de qualquer jeito (§6)

Fica para follow-up:

- [ ] BUG-01-06 — timeout configurável de verdade (membro do `Server`, não literal `60`)
- [ ] `strerror(errno)` na mensagem de accept do `Socket`, e desduplicar o log
- [ ] rodar `webserv-rules` (as novas seções 8 e 9 auditam `errno` e as flags do `fcntl`)

Antes do merge, vale rodar `tests/scripts/run-valgrind.sh conf/default.conf` — o
BUG-01-07 mexeu no ownership dos `IPollable`, então é o momento certo de confirmar
0 leaks e 0 FDs abertos (E08-T03).
