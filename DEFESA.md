# Checklist de Defesa — webserv

Grade de avaliação com o comando (ou passo a passo no navegador) que comprova cada requisito neste repositório.

**Binário:** `./webserv` · **Config de demo:** `conf/default.conf` · **Portas:** `127.0.0.1:8080` (2 vhosts) e `127.0.0.2:8081` · **I/O multiplexado:** `poll()`

---

## 1. Perguntas conceituais

O avaliador lê o código livremente e pergunta o que quiser — sem comando fixo para essa parte. Abaixo, as quatro perguntas previstas na grade.

**[oral] Explicação básica de um servidor HTTP**
> Protocolo texto de requisição/resposta sobre TCP. O cliente envia `MÉTODO URI VERSÃO`, headers, linha em branco e um body opcional; o servidor responde `VERSÃO STATUS RAZÃO`, headers, linha em branco e body.

**[comando] Função usada para multiplexação de I/O**
Resposta: `poll()`, chamado uma única vez no projeto.
```bash
grep -rn "poll(" src/
```

**[oral] Como o poll() (equivalente ao select()) funciona**
> Recebe um array de `pollfd` (fd + eventos desejados) e bloqueia até algum FD ficar pronto ou o timeout expirar. Preenche `revents` com o que de fato aconteceu (POLLIN/POLLOUT/POLLHUP/POLLERR); o servidor só chama I/O nos FDs marcados — inversão de controle do padrão Reactor.

**[oral] Um só poll(), como trata accept() e read/write do cliente**
Todo objeto com FD (socket ouvinte, cliente, pipe de CGI) implementa a mesma interface e entra no mesmo array de `pollfd`.
> O listener pede POLLIN sempre (accept); o cliente pede POLLIN enquanto lê a requisição e POLLOUT enquanto escreve a resposta. Tudo é despachado a partir de um único `poll()`, em `src/core/EventLoop.cpp:35`.

---

## 2. Regras eliminatórias do poll()

> **nota 0 imediata se qualquer item abaixo falhar**

**[comando] poll() está no loop principal e verifica leitura e escrita no mesmo array**
Um único array `pollfd[]` é montado a cada iteração com `interest()` de cada objeto (POLLIN ou POLLOUT), e uma única chamada de `poll()` cobre todos.
```bash
sed -n '1,45p' src/core/EventLoop.cpp
```

**[comando] Uma leitura ou uma escrita por cliente por poll()**
`onReadable()` faz um único `recv()`; `onWritable()` faz um único `send()`. Nenhum laço em torno deles.
```bash
grep -n "recv(\|send(" src/core/Client.cpp
```

**[comando] Erro em read/recv/write/send remove o cliente**
Nos quatro pontos de I/O do projeto (socket em Client.cpp, pipe do CGI em CgiHandler.cpp), retorno de erro marca o objeto para fechar.
```bash
grep -n "recv(\|send(" -A3 src/core/Client.cpp
grep -n "::read(\|::write(" -A3 src/cgi/CgiHandler.cpp
```

**[comando] Retorno verificado corretamente (-1 e 0, não só um dos dois)**
`Client.cpp:61` — `if (ret <= 0)` cobre EOF (0) e erro (-1) na mesma checagem.
```bash
grep -n "ret <= 0\|received <= 0\|sent < 0" src/core/Client.cpp src/cgi/CgiHandler.cpp
```

**[comando] errno NUNCA é checado após read/recv/write/send**
Toda ocorrência de `errno` no projeto é depois de `poll()`, `socket()`, `bind()` ou `listen()` — nunca depois de I/O em socket ou pipe.
```bash
grep -rn errno src/ include/
```

**[comando] Nenhum FD é lido ou escrito fora do poll()**
Os 4 únicos pontos de I/O em socket/pipe do projeto vivem dentro de `onReadable()`/`onWritable()`, chamados só pelo despacho do `EventLoop` após o `poll()` reportar prontidão. (Leitura do `.conf` é `ifstream`, antes do loop existir — não é um socket.)
```bash
grep -rn "::read(\|::write(\|recv(\|send(" src/ | grep -v "\.d:"
```

---

## 3. Compilação

**[comando] Compila sem religação desnecessária**
Segunda chamada a `make` não deve recompilar nada.
```bash
make re && make
```

---

## 4. Configuração

Servidor rodando com `./webserv conf/default.conf` em todos os testes abaixo.

**[comando] Vários sites em interfaces e portas diferentes**
```bash
curl -si http://127.0.0.1:8080/ | head -1   # 200
curl -si http://127.0.0.2:8081/ | head -1   # 200
```

**[comando] Página de erro padrão (404)**
404 mapeada em `conf/default.conf` para `/errors/404.html`. Para ver o fallback embutido do servidor, comente a linha `error_page 404 ...` do bloco e reinicie.
```bash
curl -si http://127.0.0.1:8080/rota-que-nao-existe | head -5   # 404 custom
```

**[comando] Limitar o tamanho do corpo da requisição**
`/cgi-bin` herda o limite de 1m do server; corpo > 1m deve dar 413, corpo curto deve passar.
```bash
head -c 2000000 /dev/zero | tr '\0' A | \
  curl -si -X POST -H "Content-Type: plain/text" --data-binary @- \
  http://127.0.0.1:8080/cgi-bin/post_echo.py | head -1   # 413

curl -si -X POST -H "Content-Type: plain/text" -d "corpo curto" \
  http://127.0.0.1:8080/cgi-bin/post_echo.py | head -1   # 200
```

**[comando] Rotas apontando para diretórios diferentes**
`/upload` serve de `./www/uploads`, `/cgi-bin` de `./cgi-bin`, `/` de `./www` (cada `location` com seu `root`).
```bash
grep -n "location\|root" conf/default.conf
```

**[comando] Arquivo padrão ao pedir um diretório**
`index index.html` no bloco server.
```bash
curl -si http://127.0.0.1:8080/ | head -1   # serve index.html, 200
```

**[comando] Métodos aceitos por rota (DELETE sem permissão)**
`location /` só declara `methods GET;` — DELETE deve voltar 405 com header `Allow`.
```bash
curl -si -X DELETE http://127.0.0.1:8080/ | head -6
```

---

## 5. Verificações básicas — telnet, curl, arquivos

**[comando] GET, POST e DELETE funcionam**
```bash
curl -si http://127.0.0.1:8080/                                          # 200
curl -si -X POST --data-binary @arquivo.txt http://127.0.0.1:8080/upload/arquivo.txt   # 201
curl -si -X DELETE http://127.0.0.1:8080/upload/arquivo.txt              # 204
```

**[comando] Requisição UNKNOWN não derruba o servidor**
```bash
curl -si -X FOOBAR http://127.0.0.1:8080/ | head -1   # 405 ou 501, sem crash
curl -si http://127.0.0.1:8080/ | head -1             # servidor segue vivo, 200
```

**[comando] Código de status correto em cada teste**
Referência rápida: 200 arquivo/CGI ok · 201 upload · 204 delete · 301 redirect · 400 request inválida · 403 proibido · 404 não encontrado · 405 método fora da lista · 413 corpo grande · 500/502/504 erro interno/CGI.
```bash
grep -n "case " src/common/HttpStatus.cpp | head -20
```

**[comando] Enviar um arquivo e recuperá-lo**
```bash
echo "conteudo de teste" > up.txt
curl -si -X POST --data-binary @up.txt http://127.0.0.1:8080/upload/up.txt   # 201
curl -s http://127.0.0.1:8080/upload/up.txt                                  # mesmo conteudo
```

---

## 6. CGI

**[comando] Servidor funciona corretamente com CGI, GET e POST**
```bash
curl -si http://127.0.0.1:8080/cgi-bin/hello.py             # 200, GET
curl -si -X POST -d "a=1" http://127.0.0.1:8080/cgi-bin/hello.py   # 200, POST
```

**[comando] CGI executa no diretório correto (paths relativos funcionam)**
`cgi-bin/relative.py` lê `data.txt` por caminho relativo — só funciona se o servidor der `chdir()` para a pasta do script antes do `execve`.
```bash
curl -s http://127.0.0.1:8080/cgi-bin/relative.py
```

**[comando] Script CGI com erro — tratamento correto**
`cgi-bin/broken.py` sai com código de erro sem escrever nada.
```bash
curl -si http://127.0.0.1:8080/cgi-bin/broken.py | head -1   # 502
```

**[comando] Script CGI com loop infinito — servidor não trava**
`cgi-bin/loop.py` nunca termina; o servidor deve matar o processo após o timeout (10s) e continuar respondendo a outros clientes.
```bash
curl -si http://127.0.0.1:8080/cgi-bin/loop.py | head -1   # 504, ~10s
curl -si http://127.0.0.1:8080/ | head -1                  # servidor segue vivo, 200
```

---

## 7. Verificar com o navegador

**[passo a passo] Observar headers de requisição e resposta**
1. Abrir DevTools → aba Network.
2. Navegar para `http://127.0.0.1:8080/`.
3. Clicar na requisição e inspecionar Request Headers e Response Headers.

**[passo a passo] Site totalmente estático funciona no navegador**
1. Acessar `http://127.0.0.1:8080/` e `/about.html`.
2. Confirmar que CSS (`style.css`) e JS (`main.js`) carregam sem erro no console.

**[passo a passo] URL incorreta**
1. Acessar `http://127.0.0.1:8080/pagina-que-nao-existe`.
2. Confirmar página 404 customizada e status 404 na aba Network.

**[passo a passo] Listar um diretório**
1. Acessar `http://127.0.0.1:8080/files/`.
2. Confirmar listagem gerada (autoindex on nesta location).

**[passo a passo] URL redirecionada**
1. Acessar `http://127.0.0.1:8080/old`.
2. Confirmar 301 e redirecionamento para `/` na aba Network.

---

## 8. Interfaces e portas

**[passo a passo] Várias interfaces/portas servindo sites diferentes**
1. No navegador, abrir `http://127.0.0.1:8080/` → site principal.
2. Abrir `http://127.0.0.2:8081/` → segundo site, conteúdo diferente.

**[comando] Vários sites no mesmo ip:porta (virtual host)**
Selecionado pelo header `Host`, não gera erro — abordagem válida da grade.
```bash
curl -si http://127.0.0.1:8080/ -H "Host: site-b.local" | head -1
```

**[comando] Duas instâncias com interface:porta em comum**
A segunda instância deve falhar no `bind()` de forma limpa (sem crash), não travar nem derrubar a primeira.
```bash
./webserv conf/default.conf &
sleep 1
./webserv conf/default.conf   # segunda: "Address already in use", sai com codigo 1
kill %1
```

---

## 9. Siege & teste de estresse

**[comando] Siege instalado**
Já disponível neste ambiente Linux (via apt, não Homebrew). Sem Homebrew: `sudo apt-get install -y siege`.
```bash
siege --version
```

**[comando] Disponibilidade > 99.5% em GET simples, com -b**
Combinar com a pessoa avaliadora os valores de `-c` (clientes) e `-d` antes de rodar.
```bash
siege -b -c 25 -t 30S http://127.0.0.1:8080/   # checar "Availability" no resumo final
```

**[comando] Sem vazamento de memória nem de FD**
Terminal 1 sobe o servidor sob valgrind; terminal 2 roda o siege por ~20s e encerra com Ctrl+C.
```bash
# terminal 1
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes \
  ./webserv conf/default.conf

# terminal 2
siege -b -c 25 -t 20S http://127.0.0.1:8080/
# depois: Ctrl+C no terminal 1, checar "definitely lost: 0 bytes" e "FILE DESCRIPTORS: X open (Y std)"
```

**[comando] Sem conexões pendentes após o siege**
```bash
ss -tan | grep :8080 | grep -v ESTAB   # deve voltar vazio ou quase vazio
```

**[comando] Siege repetido sem precisar reiniciar o servidor**
```bash
siege -b -c 25 -t 30S http://127.0.0.1:8080/
siege -b -c 25 -t 30S http://127.0.0.1:8080/cgi-bin/hello.py   # mesmo processo, sem restart
```

---

## 10. Bônus — cookies e sessão

> Só é avaliado se toda a parte obrigatória passou sem ressalvas.

**[comando] Sistema de sessão/cookie funcional**
`/session` devolve um cookie na primeira visita e reconhece o mesmo cliente na segunda.
```bash
curl -si -c jar.txt http://127.0.0.1:8080/session | grep -i set-cookie
curl -si -b jar.txt http://127.0.0.1:8080/session   # mesma sessao reconhecida
```
