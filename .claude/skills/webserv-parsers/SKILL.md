---
name: webserv-parsers
description: Apoio ao Membro 2 (branch feat/parsers) na implementação de ConfigParser, RequestParser e utilitários comuns (HeaderMap, StringUtils, HttpStatus). Use ao escrever ou revisar código em src/config/, src/http/RequestParser* ou src/common/.
---

# webserv-parsers — guia do Membro 2

Você está fazendo "tradução": de texto bruto (config Nginx-like e bytes do
socket) para DTOs tipados que M1 e M3 consomem.

## Princípios

1. **Parser é puro.** Recebe bytes, devolve estado. **Nunca** chama `read()`,
   `recv()`, `socket()` ou abre arquivos durante o parsing de request.
2. **Streaming, não buffering total.** `RequestParser::feed(buf, n, maxBody)`
   deve aceitar dados fragmentados (M1 vai chamar várias vezes) e retornar
   `NEED_MORE | COMPLETE | ERROR(status)`.
3. **`Request` é imutável após COMPLETE.** Apenas o parser (declarado `friend`
   de `Request`) escreve nos campos. Handlers só leem por getters `const`.
4. **Headers case-insensitive.** Use:
   ```cpp
   typedef std::map<std::string, std::string, CaseInsensitiveLess> HeaderMap;
   ```
5. **C++98 strict.** Sem `std::to_string` (use `std::stringstream`), sem
   range-for, sem `auto`.

## RequestParser — state machine mínima

```
METHOD → URI → VERSION → HEADER_KEY → HEADER_VAL → (BODY|CHUNK_SIZE|DONE)
```

Erros e códigos HTTP que **você** decide:

| Situação                                  | Status |
|-------------------------------------------|--------|
| Método desconhecido (não GET/POST/DELETE) | 501 ou 405 (depois Router decide) |
| Versão != HTTP/1.1                        | 505 |
| URI > 8192 chars                          | 414 |
| Header > 8 KB ou > 100 headers            | 431 |
| `Content-Length` ausente em POST sem chunked | 411 |
| `Content-Length` > `client_max_body_size` | 413 |
| `Transfer-Encoding` inválido / mal formado | 400 |
| CRLF malformado, bytes inválidos          | 400 |

`maxBody` vem do `LocationConfig` (passado pelo M1 quando souber qual location
casou — ou usar o do server e M3 valida de novo). Combine com a equipe.

## Chunked transfer

Decode em streaming:

```
<size em hex>\r\n<bytes>\r\n
<size em hex>\r\n<bytes>\r\n
0\r\n
\r\n
```

Cuidado: `<size>` pode ter extensions (`5;name=val`) — ignore após `;`.
Acumule no body do `Request` somente os bytes "úteis" (sem os tamanhos).

## ConfigParser — diretivas obrigatórias

```nginx
server {
    listen        <port>;          # obrigatório
    server_name   <name> [<name>]; # opcional, default ""
    root          <path>;
    index         <file> [<file>];
    client_max_body_size <size>;   # 1k, 10m, etc
    error_page <code> [<code>...] <uri>;

    location <prefix> {
        methods GET POST DELETE;   # whitelist
        autoindex on|off;
        upload_store <path>;
        return <code> <uri>;       # redirect
        cgi <ext> <interpreter>;   # ex: cgi .py /usr/bin/python3
        root <path>;               # override do server root
        index <file>;
    }
}
```

- **Múltiplos `server {}` na mesma porta** → mesmo `ListeningSocket`,
  vhost selecionado pelo header `Host` (M1 + M3 acordam isso).
- **Diretiva desconhecida** → erro de parsing, server não sobe.
- **Config inválida** → mensagem de erro clara em stderr + exit não-zero.

## Validação numérica (`client_max_body_size`)

Aceitar sufixos `k`, `m`, `g` (case-insensitive). Limites de sanidade:
1 byte mínimo, ~1 GB máximo. Overflow → erro de config.

## Common (utilities)

- `FileDescriptor` — RAII; cópia desabilitada (`private`); `release()` para transferir.
- `Logger` (Singleton) — `Logger::instance().info(...)`. Sem cores se stdout
  não é TTY. Não logue body de request (poderia conter dados sensíveis).
- `StringUtils::trim`, `split`, `toLower`, `startsWith`, `iequals`.
- `HttpStatus::reasonPhrase(int code)` — tabela completa dos status que o
  servidor retorna.

## Checklist de PR (M2)

- [ ] Parser não chama `read`/`recv`/`open` (use `grep` para confirmar)
- [ ] `Request` tem campos `private` + `friend RequestParser`
- [ ] `HeaderMap` usa `CaseInsensitiveLess`
- [ ] Edge cases 4 (413), 5 (414), 6 (chunked), 7 (411) do `webserv-test` passam
- [ ] ConfigParser rejeita config malformada com mensagem clara
- [ ] Sem uso de `std::to_string`, `nullptr`, `auto`, range-for
