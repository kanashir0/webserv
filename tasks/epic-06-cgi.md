# Épico 06 — CGI (Common Gateway Interface)

> **Donos:** Membro 1 (execução: fork/pipe/exec) + Membro 3 (env vars + parse de output). Colaboração obrigatória conforme `docs/architecture.md`.
> **Branches:** `feat/core-network` (M1) e `feat/http-logic` (M3) — integração via PR coordenado.
> **Valor entregue:** servidor capaz de executar scripts dinâmicos (Python, PHP) integrando-se ao `EventLoop` de forma **não-bloqueante**, sem `waitpid` síncrono. É o componente mais complexo do projeto e o mais cobrado em defenses.
> **Critério de "épico pronto":** `tests/configs/cgi.conf` permite GET e POST a scripts Python; `tests/cgi/post_echo.py` retorna body ecoado; `tests/cgi/env_dump.py` mostra variáveis CGI corretas; siege com 20 conexões CGI simultâneas não trava o servidor.

> **Status do épico (auditoria de 02/08/2026):** 🔴 **0 ✅ / 1 ⚠️ / 8 ❌** — **o maior bloco
> pendente do projeto.** Só o parse do output (T06) está escrito, e mesmo ele tem uma
> divergência de critério a resolver; `CgiHandler::start` e
> `CgiEnv::build` são stubs vazios e nenhum `fork()` existe no código. Consequência visível
> hoje: `GET` em script `.py` devolve o código-fonte
> ([BUG-05-01](epic-05-handlers-http.md#bug-05-01--get-em-script-cgi-devolve-o-código-fonte)).
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

> **⚠️ Escopo revisado em 02/08/2026.** Três tarefas (T04, T05, T07) tiveram a descrição
> reescrita porque a interface `IPollable` mudou desde que este épico foi redigido, e uma
> tarefa nova (T09) foi criada. **Leia as notas de escopo antes de começar** — elas resolvem
> decisões de design que estavam em aberto e que travariam o M1 no meio da implementação.

---

## ❌ E06-T01 — Implementar `CgiEnv::build` (variáveis RFC 3875)

- **Owner:** M3
- **Status:** ❌ PENDENTE — stub vazio (`src/cgi/CgiEnv.cpp:15-20`)
- **Tamanho:** M
- **Arquivos afetados:** `src/cgi/CgiEnv.cpp`, `include/cgi/CgiEnv.hpp`
- **Dependências:** E03-T02, E03-T03
- **Descrição:** popular `entries_` com strings `"KEY=VALUE"` cobrindo todas as variáveis CGI/1.1 obrigatórias e os headers HTTP convertidos para `HTTP_*`.
- **Critérios de aceite:**
  - [ ] Variáveis obrigatórias: `REQUEST_METHOD`, `SCRIPT_NAME`, `PATH_INFO`, `QUERY_STRING`, `SERVER_NAME`, `SERVER_PORT`, `SERVER_PROTOCOL`, `GATEWAY_INTERFACE` (`CGI/1.1`), `SERVER_SOFTWARE`.
  - [ ] Para POST: `CONTENT_TYPE` e `CONTENT_LENGTH` baseados nos headers da request.
  - [ ] Headers HTTP convertidos: `User-Agent: foo` → `HTTP_USER_AGENT=foo` (`-` vira `_`, uppercase).
  - [ ] `PATH_INFO` é a porção da URI após o script (`/cgi-bin/x.py/extra` → `PATH_INFO=/extra`).
  - [ ] `tests/cgi/env_dump.py` imprime as variáveis e o output bate com o esperado.
  - [ ] `REMOTE_ADDR` preenchido. ← **depende de [BUG-01-02](epic-01-motor-de-rede.md#bug-01-02--socketacceptconnection-lança-exceção-em-erro-não-eagain)**: hoje o `sockaddr_in` do `accept()` é descartado e o IP do cliente não chega a lugar nenhum. Se não for corrigido a tempo, omitir a variável (é opcional na RFC) em vez de mandar valor falso.

> **Cuidado com o `Content-Length`:** para GET sem body, `CONTENT_LENGTH` deve estar
> **ausente**, não `0` — vários scripts testam a presença da variável para decidir se leem
> o `stdin`.

---

## ❌ E06-T02 — Implementar `CgiEnv::asEnvp`

- **Owner:** M3
- **Status:** ❌ PENDENTE — a conversão está escrita (`src/cgi/CgiEnv.cpp:22-30`) e correta, mas é inútil enquanto `build()` (T01) não preencher `entries_`
- **Tamanho:** S
- **Arquivos afetados:** `src/cgi/CgiEnv.cpp`
- **Dependências:** E06-T01
- **Descrição:** converter `entries_` (vector de strings) para `char**` terminado em `NULL`, formato esperado por `execve()`. Os `char*` apontam para os `.c_str()` das strings em `entries_`, então a vida do `char**` é ligada à vida do `CgiEnv`.
- **Critérios de aceite:**
  - [x] `envp_` termina em `NULL`.
  - [x] Cada `envp_[i]` aponta para `entries_[i].c_str()`.
  - [x] `envp_` é membro do `CgiEnv` (não local) para evitar dangling pointers.
  - [x] `asVector()` permite inspecionar o conteúdo.
  - [ ] Validado com `entries_` populado por `build()`. ← só verificável depois de T01

> **Armadilha conhecida:** qualquer mutação de `entries_` depois de `asEnvp()` invalida os
> ponteiros (realloc do vector). Chamar `asEnvp()` **por último**, logo antes do `execve`.

---

## ❌ E06-T03 — Implementar `CgiHandler::start` (fork + pipe + execve)

- **Owner:** M1
- **Status:** ❌ PENDENTE — stub que só grava `startedAt_` (`src/cgi/CgiHandler.cpp:30-33`). Não existe nenhum `fork()` no projeto hoje
- **Tamanho:** XL
- **Arquivos afetados:** `src/cgi/CgiHandler.cpp`, `include/cgi/CgiHandler.hpp`
- **Dependências:** E06-T01, E06-T02
- **Descrição:** o componente mais delicado do projeto:
  1. `pipe(stdin_pipe)` e `pipe(stdout_pipe)` — 4 FDs no total.
  2. `fork()`:
     - **Filho:** `dup2(stdin_pipe[0], STDIN_FILENO)`, `dup2(stdout_pipe[1], STDOUT_FILENO)`, fechar todas as outras pontas, `chdir()` para o diretório do script (CGI espera CWD do script), `execve(interpreter, args, env.asEnvp())`. Em caso de erro, `_exit(1)` (não `exit()`, para não rodar destrutores duplicados).
     - **Pai:** fechar pontas opostas (`stdin_pipe[0]`, `stdout_pipe[1]`), guardar `stdinPipe_` (write end) e `stdoutPipe_` (read end), aplicar `setNonBlocking` em ambos, salvar `pid_` e `startedAt_`, registrar no `loop`.
- **Critérios de aceite:**
  - [ ] `fork()` é o **único** do projeto (`grep -rn 'fork(' src/` retorna só esta linha).
  - [ ] Filho fecha todos os FDs herdados que não são stdin/stdout — incluindo os sockets de escuta e os de outros clientes, senão o script segura a porta viva.
  - [ ] Pai fecha imediatamente `stdin_pipe[0]` e `stdout_pipe[1]`.
  - [ ] Em erro de `pipe`, `fork` ou `execve`, os recursos são liberados (RAII via `FileDescriptor`).
  - [ ] Funciona com Python 3 (`/usr/bin/python3`) e PHP CGI (`/usr/bin/php-cgi`).
  - [ ] `setNonBlocking` nos dois pipes usa a versão corrigida por [BUG-01-01](epic-01-motor-de-rede.md#bug-01-01--setnonblocking-descarta-as-flags-existentes-do-fd) — em pipe, apagar as flags existentes tem consequência real.

---

## ❌ E06-T04 — Implementar `CgiHandler::onReadable`, `onWritable` e `checkTimeout`

- **Owner:** M1
- **Status:** ❌ PENDENTE — **escopo ajustado em 02/08/2026** (ver nota abaixo)
- **Tamanho:** L
- **Arquivos afetados:** `src/cgi/CgiHandler.cpp`
- **Dependências:** E06-T03
- **Descrição:** integrar os pipes ao Reactor.
  - `onWritable()`: `write(stdinPipe_, body + offset, remaining)`. Quando o body inteiro for enviado, **fechar o `stdinPipe_`** para o script ver EOF — sem isso, um script que faça `sys.stdin.read()` trava para sempre.
  - `onReadable()`: `read(stdoutPipe_, buf, sizeof(buf))`. Acumular em `output_`. Retorno 0 (EOF) → `done_ = true`.
  - `checkTimeout()`: ver E06-T05.
- **Critérios de aceite:**
  - [ ] Body grande (1 MB) é entregue em múltiplos `onWritable()` sem bloquear.
  - [ ] Output grande do script é acumulado em múltiplos `onReadable()`.
  - [ ] Após `done_ = true`, o handler sinaliza conclusão ao `Client` (ver E06-T07) e só então `wantsClose_ = true`.
  - [ ] `EAGAIN` em qualquer pipe é tratado (apenas aguarda o próximo poll).
  - [ ] `interest()` devolve `POLLOUT` enquanto há body a enviar e `POLLIN` depois — hoje devolve `0` fixo (stub).
  - [ ] `checkTimeout` implementado (é puro-virtual em `IPollable`; sem ele a classe nem compila).

> **📌 Escopo ajustado — decisão de design fixada em 02/08/2026**
>
> **1. `IPollable` mudou.** A interface ganhou `virtual void checkTimeout(std::time_t now,
> std::time_t timeout) = 0;` (`include/core/IPollable.hpp:20`), que não existia quando este
> épico foi escrito. O `CgiHandler` **tem** que implementá-la — é puro-virtual. Isso é uma
> boa notícia: o timeout do CGI (E06-T05) passa a caber inteiro dentro da própria classe,
> sem código específico de CGI no `EventLoop`.
>
> **2. Dois pipes, um `fd()` por `IPollable` — decisão tomada.** O épico deixava a escolha em
> aberto e isso travaria a implementação. Fica definido: **o `CgiHandler` é o `IPollable` do
> `stdout`** (é ele quem tem o ciclo de vida longo e produz a resposta), e o `stdin` é um
> **segundo `IPollable` menor**, registrado separadamente no loop, que só escreve o body e se
> marca `wantsClose()` quando termina. Motivos: não mexe no `EventLoop` (que é a peça
> auditada pelo subject), mantém um FD por pollable, e o wrapper de stdin morre cedo pelo
> `reapClosed()` normal. O wrapper **não** deve deletar o `CgiHandler` — ver E06-T07.
>
> **3. Documentar inline.** O critério original pedia um comentário explicando a abordagem
> escolhida; continua valendo, agora com a decisão acima como referência.

---

## ❌ E06-T05 — Implementar timeout + `kill` do processo CGI

- **Owner:** M1
- **Status:** ❌ PENDENTE — **escopo simplificado em 02/08/2026** (ver nota abaixo). `timedOut()` e `kill()` já existem (`src/cgi/CgiHandler.cpp:45-51`) e estão corretos; falta o resto
- **Tamanho:** S (era M — encolheu com o ajuste de escopo)
- **Arquivos afetados:** `src/cgi/CgiHandler.cpp`
- **Dependências:** E06-T03
- **Descrição:** matar o processo CGI que passar do tempo e colher os processos terminados
  sem bloquear o loop.
- **Critérios de aceite:**
  - [ ] Script com `time.sleep(60)` e timeout de 10 s é morto após 10 s.
  - [ ] Timeout dispara `504 Gateway Timeout` para o cliente.
  - [ ] `waitpid(pid_, &status, WNOHANG)` evita zombies sem bloquear — chamado no
        `checkTimeout` e no caminho de EOF do `onReadable`.
  - [ ] `ps aux | grep python` fica zerado após `siege -c20 -t10s` contra um script CGI.
  - [ ] Default `timeoutSec = 10`.

> **📌 Escopo simplificado em 02/08/2026.** A descrição original mandava o
> `EventLoop::runOnce()` chamar `timedOut()` e tratar o kill. **Isso não é mais necessário:**
> `IPollable::checkTimeout(now, timeout)` já é chamado para todo pollable a cada tick
> (`src/core/EventLoop.cpp:45-48`), então toda a lógica cabe dentro de
> `CgiHandler::checkTimeout` — `kill(pid_, SIGKILL)`, `waitpid(WNOHANG)`, montar o 504 e
> sinalizar o `Client`. **Nenhuma linha específica de CGI entra no `EventLoop`**, o que é
> exatamente o que o subject quer ver na defesa do Reactor.
>
> **Cuidado:** o parâmetro `timeout` que chega em `checkTimeout` hoje vem em milissegundos
> por engano ([BUG-01-06](epic-01-motor-de-rede.md#bug-01-06--timeout-de-cliente-usa-milissegundos-como-segundos)).
> Corrigir esse bug **antes** desta tarefa, senão o timeout de CGI nasce quebrado do mesmo
> jeito.

---

## ⚠️ E06-T06 — Implementar `ResponseFactory::makeFromCgi` (parse de output CGI)

- **Owner:** M3
- **Status:** ⚠️ REABRIR (decisão, não defeito) — implementação sólida, mas diverge de um critério de aceite. Ver [BUG-06-01](#bug-06-01--makefromcgi-devolve-502-onde-o-critério-pedia-tolerância)
- **Tamanho:** M
- **Arquivos afetados:** `src/http/ResponseFactory.cpp`
- **Dependências:** E04-T01
- **Descrição:** parsear o output bruto do script CGI: `<headers>\r\n\r\n<body>`. Headers do CGI podem incluir `Status: 201 Created`, `Content-Type: ...`, `Location: ...`. Mapear para `Response`: status do header `Status` (default 200 se ausente), demais headers copiados, body = tudo após o separador.
- **Critérios de aceite:**
  - [x] Output `Status: 302\r\nLocation: /foo\r\n\r\n` produz redirect 302.
  - [x] `Content-Type` do CGI é preservado.
  - [x] Output 100% binário é entregue sem corrupção (o body é fatiado por índice, sem tratar como texto).
  - [x] Headers malformados → 502 Bad Gateway.
  - [x] Aceita `\n\n` além de `\r\n\r\n` como separador — scripts Python escritos à mão raramente emitem CRLF correto. **Bem lembrado.**
  - [x] `Status:` com ou sem reason-phrase (`404` ou `404 Not Found`), validando a faixa 100–599.
  - [ ] Output sem separador algum (só headers) é tolerado com body vazio. ← hoje devolve **502**; ver [BUG-06-01](#bug-06-01--makefromcgi-devolve-502-onde-o-critério-pedia-tolerância)

---

## ❌ E06-T07 — Integrar `CgiHandler` ao `Client` (assíncrono)

- **Owner:** M1 + M3
- **Status:** ❌ PENDENTE — **escopo ajustado em 02/08/2026**, com uma decisão de ownership que precisa ser tomada antes de escrever a primeira linha (ver nota abaixo)
- **Tamanho:** M
- **Arquivos afetados:** `src/core/Client.cpp`, `include/core/Client.hpp`, `src/cgi/CgiHandler.cpp`, `src/http/Router.cpp`
- **Dependências:** E06-T03, E06-T04, E06-T06, E06-T09
- **Descrição:** quando o roteamento decide por CGI: criar o `CgiHandler`, chamar
  `start(loop)`, e deixar o `Client` num estado de espera. Quando o CGI termina, o `Client`
  pega o output via `takeResponse()`, monta o `outBuffer_` e transita para
  `WRITING_RESPONSE`.
- **Critérios de aceite:**
  - [ ] O enum `Client::State` ganha `WAITING_CGI`. ← **não existe hoje** (`include/core/Client.hpp:22-28` tem apenas `DONE`, `READING_HEADERS`, `READING_BODY`, `ROUTING`, `WRITING_RESPONSE`)
  - [ ] `Client::interest()` devolve `0` em `WAITING_CGI` — o socket do cliente não é monitorado enquanto o script roda, só a conexão é mantida aberta.
  - [ ] Múltiplos clientes pedem CGI simultaneamente e são atendidos em paralelo (`siege -c20`).
  - [ ] CGI termina → resposta enviada → conexão liberada normalmente.
  - [ ] Cliente desconecta antes do CGI terminar → `kill(pid_)` e nenhum processo órfão.
  - [ ] Nenhum `delete` duplo entre `Client` e `EventLoop::reapClosed` (ver nota).

> **📌 Ownership — decidir antes de implementar.** Este é o ponto onde a integração
> tipicamente vira double-free, e o épico não tratava disso.
>
> O `EventLoop::reapClosed()` faz `delete p` em **todo** pollable que devolve
> `wantsClose() == true` (`src/core/EventLoop.cpp:80-95`). Se o `Client` também guardar um
> `CgiHandler*` e deletá-lo no destrutor, o programa quebra: o loop já deletou. Pior, a
> ordem de destruição não é determinística — o `Client` pode morrer primeiro (cliente
> desconectou) e deixar o `CgiHandler` com um ponteiro pendurado para ele.
>
> **Regra recomendada:** o `EventLoop` é o **único** dono de todo `IPollable`. O `Client`
> guarda um `CgiHandler*` apenas como *observador*, nunca deleta. Em contrapartida, o
> `CgiHandler` precisa avisar o `Client` quando terminar **e** o `Client` precisa avisar o
> `CgiHandler` se morrer antes (para o `kill`). Duas opções, escolher uma:
> - **(a)** ponteiros mútuos com invalidação explícita: cada um zera o ponteiro do outro no
>   próprio destrutor. Simples, mas exige disciplina nos dois lados.
> - **(b)** o `Client` faz *polling* de `cgi_->finished()` no seu `checkTimeout` (que já roda
>   todo tick) e o `CgiHandler` só é deletado pelo loop depois que o `Client` tiver lido o
>   `takeResponse()`. Menos acoplamento, custa um tick de latência.
>
> A **(b)** é a mais barata de acertar e é a recomendada. Registrar a escolha num comentário
> no `Client.hpp` — este é o trecho que o avaliador vai perguntar no defense.
>
> **Relacionado:** [BUG-01-07](epic-01-motor-de-rede.md#bug-01-07--eventloop-não-libera-os-ipollable-restantes-no-shutdown)
> — hoje `pollables_` já mistura dois regimes de ownership (`~Server` deleta os listeners,
> o `reapClosed` deleta os clients). Resolver isso **antes** de acrescentar um terceiro tipo.

---

## ❌ E06-T08 — Testes CGI ponta-a-ponta

- **Owner:** Todos
- **Status:** ❌ PENDENTE
- **Tamanho:** M
- **Arquivos afetados:** `tests/cgi/*.py`, `tests/scripts/curl-suite.sh`
- **Dependências:** E06-T01–E06-T07, E06-T09
- **Descrição:** estender `curl-suite.sh` com testes para CGI: GET com query, POST com body, script que retorna binário, script que retorna 500, script que demora (verificar timeout).
- **Critérios de aceite:**
  - [ ] `curl http://localhost:8080/cgi-bin/env_dump.py?foo=bar` mostra `QUERY_STRING=foo=bar`.
  - [ ] `curl --data-binary @big.bin .../cgi-bin/post_echo.py` retorna o body inalterado (`cmp` byte a byte).
  - [ ] **Nenhum GET a `.py` devolve código-fonte** — o body da resposta não pode conter `#!/usr/bin/env python3` (regressão de [BUG-05-01](epic-05-handlers-http.md#bug-05-01--get-em-script-cgi-devolve-o-código-fonte)).
  - [ ] Script com `sleep` longo → 504 dentro do prazo configurado.
  - [ ] `siege -c20 -t10s .../cgi-bin/env_dump.py` sem queda e sem zombies (`ps aux | grep python` zerado depois).
  - [ ] Valgrind sem leaks após 100 chamadas CGI.

---

## ❌ E06-T09 — Detectar CGI no `Router::route` (GET e POST)

- **Owner:** M3
- **Status:** ❌ NOVA — criada em 02/08/2026 na auditoria
- **Tamanho:** M
- **Arquivos afetados:** `src/http/Router.cpp`, `src/http/handlers/GetHandler.cpp`, `src/http/handlers/PostHandler.cpp`
- **Dependências:** E02-T04 (`loc.cgi` parseado)
- **Motivo:** o critério de "épico pronto" deste épico sempre exigiu *"GET e POST a scripts
  Python"*, mas **nenhuma tarefa cobria o GET** — a detecção de extensão CGI acabou
  implementada dentro do `PostHandler` (`findCgiInterpreter`, `PostHandler.cpp:13-24`), onde
  o `GetHandler` não a enxerga. Resultado verificado: `curl /cgi-bin/hello.py` devolve o
  código-fonte do script
  ([BUG-05-01](epic-05-handlers-http.md#bug-05-01--get-em-script-cgi-devolve-o-código-fonte)).
  Lacuna de planejamento, não regressão → tarefa nova.
- **Descrição:** mover a detecção de CGI de dentro do `PostHandler` para o `Router::route`,
  logo após o `methodAllowed` e antes do dispatch por método. Se a extensão do path bater com
  uma chave de `loc.cgi`, o fluxo vai para o CGI independentemente do método; senão, segue
  para o handler estático de sempre. A função `findCgiInterpreter` já existe e só precisa
  mudar de casa (reaproveitar, não reescrever).
- **Critérios de aceite:**
  - [ ] `GET /cgi-bin/hello.py` **executa** o script; o body da resposta não contém o
        código-fonte.
  - [ ] `POST /cgi-bin/post_echo.py` continua funcionando (não pode regredir E05-T03).
  - [ ] `GET /cgi-bin/naoexiste.py` → 404, sem tentar executar.
  - [ ] Arquivo `.py` numa location **sem** diretiva `cgi` continua sendo servido como
        estático — a detecção é por `loc.cgi`, nunca pela extensão sozinha.
  - [ ] `DELETE` numa location com `cgi` **não** executa o script; segue para o
        `DeleteHandler` (ou 405, conforme `methods`).
  - [ ] `findCgiInterpreter` fica num único lugar, sem cópia duplicada nos handlers.
  - [ ] `PATH_INFO` continua correto quando o path tem sufixo (`/cgi-bin/x.py/extra`) — a
        detecção precisa achar a extensão no **meio** do path, não só no fim (hoje o
        `endsWith` só pega no fim; combinar com E06-T01).

---

## Resumo de tarefas

| ID | Tarefa | Owner | Status | Tamanho | Dependências |
|----|--------|-------|--------|---------|-------------|
| E06-T01 | CgiEnv::build | M3 | ❌ | M | E03 |
| E06-T02 | CgiEnv::asEnvp | M3 | ❌ (bloqueada por T01) | S | T01 |
| E06-T03 | CgiHandler::start | M1 | ❌ | XL | T01, T02 |
| E06-T04 | onReadable/onWritable/checkTimeout | M1 | ❌ escopo ajustado | L | T03 |
| E06-T05 | timeout + kill | M1 | ❌ escopo simplificado | S | T03 |
| E06-T06 | makeFromCgi | M3 | ⚠️ BUG-06-01 | M | E04-T01 |
| E06-T07 | Integração Client+CGI | M1+M3 | ❌ escopo ajustado | M | T03, T04, T06, T09 |
| E06-T08 | Testes ponta-a-ponta | Todos | ❌ | M | T01–T07, T09 |
| E06-T09 | Detecção de CGI no Router | M3 | ❌ nova | M | E02-T04 |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`.

### BUG-06-01 — `makeFromCgi` devolve 502 onde o critério pedia tolerância

- **Origem:** E06-T06
- **Onde:** `src/http/ResponseFactory.cpp:287-293`
- **Sintoma:** um output CGI sem nenhum separador de headers (nem `\r\n\r\n`, nem `\n\n`)
  resulta em `502 Bad Gateway`. O critério de aceite de E06-T06 dizia
  *"output sem `\r\n\r\n` (apenas headers) é tolerado (body vazio)"*.
- **Discussão:** a implementação é defensável — a RFC 3875 §6.2 exige a linha em branco, e
  sem ela não há como distinguir header de body. Tratar como erro de gateway é o que Apache
  e Nginx fazem. Por outro lado, o critério original queria ser permissivo com scripts
  escritos à mão.
- **Decisão necessária (M3):** manter o 502 e **reescrever o critério de aceite**
  (recomendado), ou afrouxar o parser para tratar todo o output como header block com body
  vazio. Só afeta scripts malformados; qualquer script correto emite a linha em branco.
- **Severidade:** Baixa — é uma divergência de especificação, não um defeito. Está marcada
  para não passar batido numa revisão de critérios.

---

Fora isso, **nenhum bug de implementação neste épico** — quase nada foi implementado, então
não há o que estar quebrado. O que existe são **quatro ajustes de escopo** já incorporados
às tarefas acima, listados aqui para quem só varre esta seção:

| Ajuste | Onde | O que mudou |
|--------|------|-------------|
| `IPollable::checkTimeout` é novo e puro-virtual | E06-T04 | `CgiHandler` tem que implementá-lo ou nem compila |
| Decisão dos 2 pipes vs. 1 fd por pollable | E06-T04 | Fixada: `CgiHandler` é o pollable do `stdout`; `stdin` vira um segundo pollable menor |
| Timeout sai do `EventLoop` | E06-T05 | Cabe inteiro em `CgiHandler::checkTimeout`; tamanho caiu de M para S |
| Ownership `Client` ↔ `CgiHandler` | E06-T07 | O `EventLoop` é o dono único; o `Client` só observa. Escolher entre ponteiros mútuos (a) ou polling de `finished()` (b) — recomendada a (b) |

E três bugs **de fora** que precisam estar fechados antes de começar:

- [BUG-01-06](epic-01-motor-de-rede.md#bug-01-06--timeout-de-cliente-usa-milissegundos-como-segundos)
  — o `checkTimeout` recebe milissegundos como se fossem segundos. Se não for corrigido, o
  timeout de CGI (E06-T05) nasce quebrado do mesmo jeito.
- [BUG-01-07](epic-01-motor-de-rede.md#bug-01-07--eventloop-não-libera-os-ipollable-restantes-no-shutdown)
  — `pollables_` já mistura dois regimes de ownership. Acrescentar um terceiro tipo de
  pollable sem resolver isso é convite a double-free (ver E06-T07).
- [BUG-01-01](epic-01-motor-de-rede.md#bug-01-01--setnonblocking-descarta-as-flags-existentes-do-fd)
  — `setNonBlocking` apaga as flags do FD. Inofensivo em socket novo, relevante em pipe.
