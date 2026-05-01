# Arquitetura — Webserv

Documento de referência para os 3 membros. Plano completo em
`/home/gkana/.claude/plans/voc-um-engenheiro-agile-sunrise.md`.

## Pilares

- **Reactor (`EventLoop`)**: único `poll()` central; despacha para `IPollable`.
- **State Machine**: `Client::State` (READING_HEADERS → … → DONE) e
  `RequestParser::State` (METHOD → … → DONE).
- **DTO**: `Request`, `Response`, `ServerConfig`. Imutáveis após construção.
- **Strategy**: `IMethodHandler` → GET/POST/DELETE.
- **Factory**: `ResponseFactory::makeError/makeFile/makeAutoindex/makeRedirect/makeFromCgi`.
- **Singleton**: `Logger::instance()`.
- **RAII**: `FileDescriptor`, `Socket`.

## Fluxo de uma requisição

```
ListeningSocket.onReadable()           [Membro 1]
  └─ accept() → new Client → loop.add(client)

Client.onReadable()                    [Membro 1]
  └─ recv() → parser_.feed(buf, n, maxBody)
       ├─ NEED_MORE  → continua
       ├─ COMPLETE   → state_ = ROUTING
       └─ erro       → ResponseFactory::makeError(...)

Client (ROUTING)                       [Membro 3]
  └─ router_.route(request_, vhost) → response_
       └─ outBuffer_ = response_.toString()

Client.onWritable()                    [Membro 1]
  └─ send(outBuffer_) → state_ = DONE → wantsClose_ = true
```

## Ownership

| Módulo                              | Dono       |
|-------------------------------------|------------|
| `core/`, partes de execução `cgi/`  | Membro 1   |
| `config/`, `http/RequestParser*`, `common/` | Membro 2 |
| `http/Router*`, `http/handlers/*`, `http/Response*`, `http/ResponseFactory*`, `http/MimeTypes*`, `session/*` | Membro 3 |
