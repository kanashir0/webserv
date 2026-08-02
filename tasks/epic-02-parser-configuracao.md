# Épico 02 — Parser de Configuração

> **Dono primário:** Membro 2 (M2)
> **Branch:** `feat/parsers`
> **Valor entregue:** capacidade de carregar um arquivo `.conf` no estilo Nginx e transformá-lo em uma estrutura `vector<ServerConfig>` consumível pelos demais módulos. Sem este épico, o servidor não sabe em qual porta escutar, qual diretório servir nem qual `client_max_body_size` aplicar.
> **Critério de "épico pronto":** `./webserv conf/default.conf`, `./webserv tests/configs/basic.conf`, `./webserv tests/configs/multi-server.conf` e `./webserv tests/configs/cgi.conf` carregam sem erro e produzem `ServerConfig` corretos para cada teste do `curl-suite.sh`.

> **Status do épico (auditoria de 02/08/2026):** 🟢 **5 ✅ / 2 ⚠️ / 1 ❌** — o parser está
> sólido e carrega todas as configs do repositório. Faltam validações semânticas, a exibição
> do número da linha no erro, e a cobertura de testes (reescopada). Ver [Bugs e ajustes abertos](#bugs-e-ajustes-abertos).
> Legenda: ✅ feita e correta · ⚠️ feita, precisa reabrir · ❌ não iniciada.

---

## ✅ E02-T01 — Implementar `ConfigParser::parseFile`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/config/ConfigParser.cpp`, `include/config/ConfigParser.hpp`
- **Dependências:** nenhuma
- **Descrição:** abrir o arquivo no path passado, ler todo o conteúdo para `source_` (string), delegar para `parseString(source_)`. Lançar `ParseError` se o arquivo não existir ou não puder ser lido.
- **Critérios de aceite:**
  - [x] Erro de I/O gera `ParseError` com mensagem clara e linha `0`.
  - [x] Arquivo vazio resulta em `vector<ServerConfig>` vazio (sem exceção).
  - [x] Path com `~` ou caracteres especiais é tratado como literal (sem expansão de shell).

> **Extra não previsto na tarefa:** o `stat()` antes do `ifstream` distingue "path é um
> diretório" de "arquivo vazio" — um `ifstream` abre diretório sem erro e devolve zero
> bytes, o que seria indistinguível de um `.conf` vazio (que é válido). Bom detalhe.

---

## ✅ E02-T02 — Implementar tokenizer (`nextToken`, `expect`, `skipWhitespace`)

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** M
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T01
- **Descrição:** `skipWhitespace()` avança `pos_` sobre espaços, tabs, newlines e comentários (`#` até fim da linha), incrementando `line_` ao encontrar `\n`. `nextToken()` chama `skipWhitespace()` e retorna a próxima palavra ou símbolo (`{`, `}`, `;`). `expect(token)` chama `nextToken()` e lança `ParseError` se diferente.
- **Critérios de aceite:**
  - [x] Comentários `#` até fim de linha são ignorados.
  - [x] `line_` é incrementado corretamente — `ParseError::line()` aponta para a linha real do erro.
  - [x] Símbolos `{`, `}`, `;` são tokens isolados mesmo sem espaço antes.
  - [ ] Strings entre aspas (`"path with space"`) são tratadas como token único. ← não implementado; era marcado como **opcional** na tarefa original, e nenhuma config do projeto precisa
  - [x] EOF inesperado dentro de bloco gera `ParseError` clara.

---

## ✅ E02-T03 — Implementar `doParse` e `parseServerBlock`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** L
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T02
- **Descrição:** `doParse()` é o loop top-level: lê tokens até EOF; para cada `server`, chama `expect("{")` e `parseServerBlock()`. `parseServerBlock()` lê diretivas (`listen`, `server_name`, `root`, `index`, `client_max_body_size`, `error_page`) e blocos `location { ... }` até encontrar `}`.
- **Critérios de aceite:**
  - [x] Tokens fora de um bloco `server` geram `ParseError`.
  - [x] Cada `server { ... }` produz exatamente um `ServerConfig` no vetor de saída.
  - [x] `listen 8080` parseia para `port=8080, host="0.0.0.0"`.
  - [x] `listen 127.0.0.1:8080` parseia para `host="127.0.0.1", port=8080`.
  - [x] `server_name a.local b.local` produz `serverNames=["a.local", "b.local"]`.
  - [x] `client_max_body_size 1m` produz `1048576` (sufixos `k`, `m`, `g` case-insensitive).
  - [x] `error_page 404 /errors/404.html` insere `errorPages[404] = "/errors/404.html"`.

---

## ✅ E02-T04 — Implementar `parseLocationBlock`

- **Owner:** M2
- **Status:** ✅ CONCLUÍDA — atenção ao nome da diretiva de redirect, ver [BUG-02-02](#bug-02-02--a-diretiva-de-redirect-chama-se-return-mas-a-documentação-diz-redirect)
- **Tamanho:** L
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T03
- **Descrição:** parsear `location <path> { ... }` reconhecendo as diretivas: `methods`, `root`, `index`, `autoindex`, `return`, `upload_store`, `cgi`, `client_max_body_size`. Adicionar um `LocationConfig` ao `ServerConfig::locations`.
- **Critérios de aceite:**
  - [x] `methods GET POST DELETE` produz `methods=["GET", "POST", "DELETE"]` (normalizados para maiúsculas).
  - [x] `autoindex on` → `true`; `autoindex off` → `false`; outro valor → `ParseError`.
  - [x] `return 301 /new` produz `redirect="/new", redirectCode=301`. ← a diretiva é `return` (estilo Nginx), **não** `redirect`; o campo do struct é que se chama `redirect`
  - [x] `cgi .py /usr/bin/python3` insere `cgi[".py"] = "/usr/bin/python3"` (e valida que a extensão começa com `.`).
  - [x] Múltiplas diretivas `cgi` em um mesmo location são acumuladas no map.
  - [x] `client_max_body_size` em location sobrescreve a do server para aquele scope.

---

## ⚠️ E02-T05 — Validação semântica das diretivas

- **Owner:** M2
- **Status:** ⚠️ REABRIR — ver [BUG-02-01](#bug-02-01--três-validações-semânticas-de-e02-t05-não-foram-implementadas)
- **Tamanho:** M
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T03, E02-T04
- **Descrição:** após parsear todos os blocos, validar regras de negócio: porta entre 1–65535; `methods` contém apenas valores válidos (`GET`, `POST`, `DELETE`); `redirectCode` é 301 ou 302; pelo menos um `server` foi definido; `upload_store` (se definido) é um diretório existente.
- **Critérios de aceite:**
  - [x] Porta `0` ou `> 65535` gera `ParseError`.
  - [x] `methods PUT` gera `ParseError` com o nome do método.
  - [ ] `return 999 /foo` gera `ParseError` (código inválido). ← `parseStatusCode` só valida a faixa 100–599; qualquer código dessa faixa passa, e só o `ResponseFactory::makeRedirect` rejeita depois (virando 500 em runtime em vez de erro de config)
  - [ ] Arquivo `.conf` sem nenhum `server` gera `ParseError`. ← hoje retorna vetor vazio e o servidor sobe sem escutar em lugar nenhum
  - [x] Diretiva desconhecida (ex: `foobar baz;`) gera `ParseError` com nome da diretiva.
  - [ ] `upload_store` (se definido) é um diretório existente. ← não validado

---

## ✅ E02-T06 — Implementar `ServerConfig::findLocation` (longest-prefix match)

- **Owner:** M2 (em colaboração com M3)
- **Status:** ✅ CONCLUÍDA
- **Tamanho:** S
- **Arquivos afetados:** `src/config/ServerConfig.cpp`
- **Dependências:** E02-T04
- **Descrição:** percorrer `locations` e retornar ponteiro para o `LocationConfig` cujo `path` é o **maior prefixo** de `uriPath`. Se nenhum bater, retornar `NULL`. Em caso de empate de prefixo, vence o primeiro declarado no `.conf` (ordem do vetor).
- **Critérios de aceite:**
  - [x] `findLocation("/upload/foo.png")` com locations `/`, `/upload` retorna `/upload`.
  - [x] `findLocation("/")` com location `/` retorna `/`.
  - [x] `findLocation("/cgi-bin/x.py")` com locations `/`, `/cgi-bin` retorna `/cgi-bin`.
  - [x] `findLocation("/x")` sem nenhum location bater retorna `NULL` (router transforma em 404).
  - [x] Empate de prefixo: vence o primeiro declarado (`>` estrito na comparação de tamanho).
  - [x] Match é por prefixo puro, sem exigir fronteira de segmento — `location /up` **casa** com `/upload`. ← **critério corrigido em 02/08/2026**: o texto original dizia o contrário (`/up` não bate em `/upload`), mas o comportamento implementado é o do Nginx, que é o que o subject pede. A implementação está certa; o critério é que estava errado

---

## ⚠️ E02-T07 — Tratamento de `ParseError` com número de linha

- **Owner:** M2
- **Status:** ⚠️ REABRIR — ver [BUG-02-03](#bug-02-03--o-número-da-linha-do-parseerror-nunca-chega-ao-usuário)
- **Tamanho:** S
- **Arquivos afetados:** `src/config/ConfigParser.cpp`, `src/main.cpp`
- **Dependências:** E02-T02
- **Descrição:** garantir que toda `ParseError` carrega `line_` correto. No `main.cpp`, capturar `ParseError` e exibir a mensagem antes de sair com `EXIT_FAILURE`.
- **Critérios de aceite:**
  - [x] Toda `ParseError` carrega `line_`.
  - [x] `main()` retorna código de saída ≠ 0 em caso de erro.
  - [x] Mensagem contém o token problemático ou a diretiva inválida.
  - [ ] Saída no formato `[ERROR] arquivo.conf:linha: mensagem`. ← o `main.cpp` captura `std::exception` genérico e imprime só `fatal: <what()>`; o número da linha existe no objeto mas **nunca é exibido**

---

## ❌ E02-T08 — Cobertura do `ConfigParser` no `test-edge-cases.sh`

- **Owner:** M2
- **Status:** ❌ PENDENTE — **reescopada em 02/08/2026** (era: "Testes unitários do `ConfigParser`" em `tests/unit/test_config_parser.cpp`)
- **Tamanho:** S (era M)
- **Arquivos afetados:** `tests/scripts/test-edge-cases.sh`, `tests/configs/*.conf`
- **Dependências:** E02-T01–E02-T07
- **Motivo do reescopo:** o padrão de teste do projeto é `curl-suite.sh` + `test-edge-cases.sh` (ver a política em [`epic-08-qualidade-testes.md`](epic-08-qualidade-testes.md)). Não haverá testes unitários em C++: a 42 avalia comportamento, o projeto não tem harness e criar um é custo que não paga.
- **Descrição:** validar o parser pela porta da frente — subir o `./webserv` com cada config de `tests/configs/` e com configs propositalmente inválidas, verificando o exit code e a mensagem de erro no stderr.
- **Critérios de aceite:**
  - [ ] Cada `.conf` de `tests/configs/` sobe o servidor com exit code 0 e responde a um `curl` de sanidade.
  - [ ] Pelo menos 6 configs inválidas (porta fora de faixa, diretiva desconhecida, `}` faltando, `;` faltando, `methods PUT`, `autoindex talvez`) fazem o binário sair com código ≠ 0.
  - [ ] Cada caso negativo verifica a mensagem **e o número da linha** no stderr (depende de E02-T07 exibir a linha).
  - [ ] Invocável por `make test`.

---

## Resumo de tarefas

| ID | Tarefa | Status | Tamanho | Dependências |
|----|--------|--------|---------|-------------|
| E02-T01 | parseFile | ✅ | S | — |
| E02-T02 | tokenizer | ✅ | M | T01 |
| E02-T03 | doParse + parseServerBlock | ✅ | L | T02 |
| E02-T04 | parseLocationBlock | ✅ | L | T03 |
| E02-T05 | Validação semântica | ⚠️ BUG-02-01 | M | T03, T04 |
| E02-T06 | findLocation | ✅ | S | T04 |
| E02-T07 | ParseError com linha | ⚠️ BUG-02-03 | S | T02 |
| E02-T08 | Cobertura no test-edge-cases.sh | ❌ reescopada | S | T01–T07 |

---

## Bugs e ajustes abertos

> Levantados na auditoria de 02/08/2026 sobre a branch `feat/request-pipeline`.

### BUG-02-01 — Três validações semânticas de E02-T05 não foram implementadas

- **Origem:** E02-T05
- **Onde:** `src/config/ConfigParser.cpp`
- **Sintoma:** o parser valida bem a **sintaxe** (diretiva desconhecida, `;` faltando, faixa
  de porta, `methods` inválido), mas três regras de negócio da tarefa ficaram de fora:
  1. **`upload_store` não é checado como diretório existente** (`ConfigParser.cpp:273-275`).
     O erro só aparece em runtime, no primeiro POST, como um `500` genérico — foi
     exatamente o que aconteceu no smoke test (`./www/uploads` não existe no repositório,
     ver [BUG-05-02](epic-05-handlers-http.md#bugs-e-ajustes-abertos)).
  2. **`return <código>` aceita qualquer valor entre 100 e 599** (`ConfigParser.cpp:265-272`
     via `parseStatusCode`). Um `return 999` é barrado, mas um `return 404 /foo` passa no
     parsing e só é rejeitado em runtime pelo `ResponseFactory::makeRedirect`, que devolve
     500 ao cliente. Deveria ser `ParseError` no startup, restrito a 301/302.
  3. **`.conf` sem nenhum bloco `server` não gera erro** (`ConfigParser.cpp:165-177`):
     `doParse()` devolve um vetor vazio, o `Server::start` não cria listener nenhum e o
     processo fica parado num `EventLoop` sem pollables, aparentemente vivo.
- **Esperado:** as três validadas no fim do parsing, cada uma com `ParseError` e linha.
  Falhar no startup é muito mais barato de diagnosticar do que um 500 ou um servidor mudo.
- **Severidade:** Baixa individualmente; juntas transformam três bugs obscuros em erros
  óbvios de configuração.

### BUG-02-03 — O número da linha do `ParseError` nunca chega ao usuário

- **Origem:** E02-T07
- **Onde:** `src/main.cpp:35-37`
- **Sintoma:** o `ConfigParser` preenche `ParseError::line_` corretamente em todos os
  caminhos, mas o `main()` captura `const std::exception&` genérico e imprime apenas
  `fatal: <what()>`. O número da linha — a parte mais útil da mensagem — é calculado e
  descartado.
- **Esperado:** um `catch (const ConfigParser::ParseError& e)` antes do catch genérico,
  imprimindo `[ERROR] <arquivo>:<linha>: <mensagem>` conforme a tarefa.
- **Severidade:** Baixa — mas é o critério de aceite mais visível de E02-T07, e
  E02-T08 depende dele para validar os casos negativos.

### BUG-02-02 — A diretiva de redirect chama-se `return`, mas a documentação diz `redirect`

- **Origem:** E02-T04
- **Onde:** `src/config/ConfigParser.cpp:265` (implementado como `return`, estilo Nginx)
- **Sintoma:** o parser reconhece `return 301 /new;`. A documentação do projeto descreve a
  diretiva como `redirect 301 /new;`, que geraria `ParseError: unknown directive`. A
  confusão é fácil porque o **campo do struct** realmente se chama `redirect`
  (`LocationConfig::redirect` / `redirectCode`) — só o nome da diretiva no `.conf` é que é
  `return`.
- **Alcance:** as menções nos arquivos de tarefa (`epic-02` T04, `epic-04` T03) já foram
  corrigidas nesta rodada. **Continuam desatualizados e fora do escopo desta edição:**
  `CLAUDE.md` (seção "Configuração (estilo Nginx)") e `README.md`.
- **Esperado:** alinhar `CLAUDE.md` e `README.md` com o parser (`return`), ou renomear a
  diretiva no parser para `redirect`. Recomendado manter `return` — é o nome do Nginx, e o
  subject pede comportamento "estilo Nginx".
- **Severidade:** Baixa — mas custa tempo de quem escrever um `.conf` seguindo o `CLAUDE.md`.
