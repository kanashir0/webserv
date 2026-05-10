# Épico 02 — Parser de Configuração

> **Dono primário:** Membro 2 (M2)
> **Branch:** `feat/parsers`
> **Valor entregue:** capacidade de carregar um arquivo `.conf` no estilo Nginx e transformá-lo em uma estrutura `vector<ServerConfig>` consumível pelos demais módulos. Sem este épico, o servidor não sabe em qual porta escutar, qual diretório servir nem qual `client_max_body_size` aplicar.
> **Critério de "épico pronto":** `./webserv conf/default.conf`, `./webserv tests/configs/basic.conf`, `./webserv tests/configs/multi-server.conf` e `./webserv tests/configs/cgi.conf` carregam sem erro e produzem `ServerConfig` corretos para cada teste do `curl-suite.sh`.

---

## E02-T01 — Implementar `ConfigParser::parseFile`

- **Owner:** M2
- **Tamanho:** S
- **Arquivos afetados:** `src/config/ConfigParser.cpp`, `include/config/ConfigParser.hpp`
- **Dependências:** nenhuma
- **Descrição:** abrir o arquivo no path passado, ler todo o conteúdo para `source_` (string), delegar para `parseString(source_)`. Lançar `ParseError` se o arquivo não existir ou não puder ser lido.
- **Critérios de aceite:**
  - [ ] Erro de I/O gera `ParseError` com mensagem `"não foi possível abrir o arquivo: <path>"` e linha `0`.
  - [ ] Arquivo vazio resulta em `vector<ServerConfig>` vazio (sem exceção).
  - [ ] Path com `~` ou caracteres especiais é tratado como literal (sem expansão de shell).

---

## E02-T02 — Implementar tokenizer (`nextToken`, `expect`, `skipWhitespace`)

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T01
- **Descrição:** `skipWhitespace()` avança `pos_` sobre espaços, tabs, newlines e comentários (`#` até fim da linha), incrementando `line_` ao encontrar `\n`. `nextToken()` chama `skipWhitespace()` e retorna a próxima palavra ou símbolo (`{`, `}`, `;`). `expect(token)` chama `nextToken()` e lança `ParseError` se diferente.
- **Critérios de aceite:**
  - [ ] Comentários `#` até fim de linha são ignorados.
  - [ ] `line_` é incrementado corretamente — `ParseError::line()` aponta para a linha real do erro.
  - [ ] Símbolos `{`, `}`, `;` são tokens isolados mesmo sem espaço antes.
  - [ ] Strings entre aspas (`"path with space"`) são tratadas como token único (opcional, mas recomendado).
  - [ ] EOF inesperado dentro de bloco gera `ParseError` clara.

---

## E02-T03 — Implementar `doParse` e `parseServerBlock`

- **Owner:** M2
- **Tamanho:** L
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T02
- **Descrição:** `doParse()` é o loop top-level: lê tokens até EOF; para cada `server`, chama `expect("{")` e `parseServerBlock()`. `parseServerBlock()` lê diretivas (`listen`, `server_name`, `root`, `index`, `client_max_body_size`, `error_page`) e blocos `location { ... }` até encontrar `}`.
- **Critérios de aceite:**
  - [ ] Tokens fora de um bloco `server` (exceto whitespace/comentário) geram `ParseError`.
  - [ ] Cada `server { ... }` produz exatamente um `ServerConfig` no vetor de saída.
  - [ ] `listen 8080` parseia para `port=8080, host="0.0.0.0"`.
  - [ ] `listen 127.0.0.1:8080` parseia para `host="127.0.0.1", port=8080`.
  - [ ] `server_name a.local b.local` produz `serverNames=["a.local", "b.local"]`.
  - [ ] `client_max_body_size 1m` produz `1048576` (suporta sufixos `k`, `m`, `g` case-insensitive).
  - [ ] `error_page 404 /errors/404.html` insere `errorPages[404] = "/errors/404.html"`.

---

## E02-T04 — Implementar `parseLocationBlock`

- **Owner:** M2
- **Tamanho:** L
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T03
- **Descrição:** parsear `location <path> { ... }` reconhecendo as diretivas: `methods`, `root`, `index`, `autoindex`, `redirect`, `upload_store`, `cgi`, `client_max_body_size`. Adicionar um `LocationConfig` ao `ServerConfig::locations`.
- **Critérios de aceite:**
  - [ ] `methods GET POST DELETE` produz `methods=["GET", "POST", "DELETE"]`.
  - [ ] `autoindex on` → `true`; `autoindex off` → `false`; outro valor → `ParseError`.
  - [ ] `redirect 301 /new` produz `redirect="/new", redirectCode=301`.
  - [ ] `cgi .py /usr/bin/python3` insere `cgi[".py"] = "/usr/bin/python3"`.
  - [ ] Múltiplas diretivas `cgi` em um mesmo location são acumuladas no map.
  - [ ] `client_max_body_size` em location sobrescreve a do server para aquele scope.

---

## E02-T05 — Validação semântica das diretivas

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `src/config/ConfigParser.cpp`
- **Dependências:** E02-T03, E02-T04
- **Descrição:** após parsear todos os blocos, validar regras de negócio: porta entre 1–65535; `methods` contém apenas valores válidos (`GET`, `POST`, `DELETE`); `redirectCode` é 301 ou 302; pelo menos um `server` foi definido; `upload_store` (se definido) é um diretório existente.
- **Critérios de aceite:**
  - [ ] Porta `0` ou `> 65535` gera `ParseError`.
  - [ ] `methods PUT` gera `ParseError` ou warning configurável (preferir erro).
  - [ ] `redirect 999 /foo` gera `ParseError` (código inválido).
  - [ ] Arquivo `.conf` sem nenhum `server` gera `ParseError`.
  - [ ] Diretiva desconhecida (ex: `foobar baz;`) gera `ParseError` com nome da diretiva.

---

## E02-T06 — Implementar `ServerConfig::findLocation` (longest-prefix match)

- **Owner:** M2 (em colaboração com M3)
- **Tamanho:** S
- **Arquivos afetados:** `src/config/ServerConfig.cpp`
- **Dependências:** E02-T04
- **Descrição:** percorrer `locations` e retornar ponteiro para o `LocationConfig` cujo `path` é o **maior prefixo** de `uriPath`. Se nenhum bater, retornar `nullptr`. Em caso de empate de prefixo, vence o primeiro declarado no `.conf` (ordem do vetor).
- **Critérios de aceite:**
  - [ ] `findLocation("/upload/foo.png")` com locations `/`, `/upload` retorna `/upload`.
  - [ ] `findLocation("/")` com location `/` retorna `/`.
  - [ ] `findLocation("/cgi-bin/x.py")` com locations `/`, `/cgi-bin` retorna `/cgi-bin`.
  - [ ] `findLocation("/x")` sem nenhum location bater retorna `nullptr` (router transformará em 404).
  - [ ] Match é exato no início — `/up` **não** bate em `/upload`.

---

## E02-T07 — Tratamento de `ParseError` com número de linha

- **Owner:** M2
- **Tamanho:** S
- **Arquivos afetados:** `src/config/ConfigParser.cpp`, `src/main.cpp`
- **Dependências:** E02-T02
- **Descrição:** garantir que toda `ParseError` carrega `line_` correto. No `main.cpp`, capturar `ParseError` e exibir `[ERROR] arquivo.conf:linha: mensagem` antes de sair com `EXIT_FAILURE`.
- **Critérios de aceite:**
  - [ ] Erro em `tests/configs/basic.conf` linha 14 reporta `basic.conf:14`.
  - [ ] `main()` retorna código de saída ≠ 0 em caso de erro.
  - [ ] Mensagem contém o token problemático ou diretiva inválida.

---

## E02-T08 — Testes unitários do `ConfigParser`

- **Owner:** M2
- **Tamanho:** M
- **Arquivos afetados:** `tests/unit/test_config_parser.cpp` (novo) ou `tests/scripts/test-config.sh`
- **Dependências:** E02-T01–E02-T07
- **Descrição:** criar suite de testes que invoca `parseString()` com strings inline cobrindo: config válida mínima, config com múltiplos servers/locations, configs inválidas (espera `ParseError`), config com error_pages, config com CGI.
- **Critérios de aceite:**
  - [ ] Pelo menos 10 casos de teste (positivos e negativos).
  - [ ] Casos negativos verificam mensagem e linha do erro.
  - [ ] Suite é executável via `make test` ou script dedicado.
  - [ ] Passa em todos os arquivos de `tests/configs/*.conf`.

---

## Resumo de tarefas

| ID | Tarefa | Tamanho | Dependências |
|----|--------|---------|-------------|
| E02-T01 | parseFile | S | — |
| E02-T02 | tokenizer | M | T01 |
| E02-T03 | doParse + parseServerBlock | L | T02 |
| E02-T04 | parseLocationBlock | L | T03 |
| E02-T05 | Validação semântica | M | T03, T04 |
| E02-T06 | findLocation | S | T04 |
| E02-T07 | ParseError com linha | S | T02 |
| E02-T08 | Testes unitários | M | T01–T07 |
