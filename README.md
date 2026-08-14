*This project was created as part of the 42 curriculum by <cbrito-s>, <gyasuhir>, <acesar-m>.*

# webserv

## Description

**webserv** is an HTTP/1.1 web server written from scratch in **C++98**, with no
external libraries. It serves static websites, runs CGI scripts, accepts file
uploads from clients and can host several websites at once — all from a single
process.

The server is **single-threaded and fully non-blocking**. Every socket and every
CGI pipe is driven by one `poll()` call in the main event loop: the loop asks
`poll()` which file descriptors are ready for reading or writing, and only then
performs the corresponding I/O. Nothing ever blocks, and no request can stall
the server for another client.config/ 

Behaviour is entirely driven by an nginx-inspired configuration file, so the same
binary can serve very different setups without recompiling.

### Features

- `GET`, `POST` and `DELETE`
- Multiple `interface:port` pairs, and virtual hosts sharing one port (selected
  by the `Host` header)
- Per-route configuration: allowed methods, alternate document root, directory
  listing, default index file, HTTP redirects, upload destination
- Custom error pages, with sensible built-in defaults when none is configured
- Request body size limits, per server and per route
- CGI execution based on file extension (Python out of the box), for both `GET`
  and `POST`, including `Transfer-Encoding: chunked` request bodies
- File uploads, including `multipart/form-data`
- Keep-alive connections and request pipelining
- **Bonus:** cookie-based session management

## Instructions

### Requirements

A C++98-capable compiler (`c++`/`g++`/`clang++`), GNU `make`, and `python3` for
the CGI examples. Development and testing were done on Linux.

### Build

```bash
make            # builds ./webserv
make re         # rebuild from scratch
make clean      # remove object files
make fclean     # remove object files and the binary
```

The mandatory flags `-Wall -Wextra -Werror -std=c++98 -pedantic` are always on
and the build is warning-free.

### Run

```bash
./webserv                        # uses conf/default.conf
./webserv conf/default.conf      # explicit configuration file
```

`conf/default.conf` starts three sites and exercises every mandatory feature:

| URL | What it shows |
|---|---|
| <http://127.0.0.1:8080/> | Static website |
| <http://127.0.0.1:8080/files/> | Directory listing (`autoindex on`) |
| <http://127.0.0.1:8080/old> | `301` redirect |
| <http://127.0.0.1:8080/nope> | Custom `404` page |
| <http://127.0.0.1:8080/upload/> | File upload and download |
| <http://127.0.0.1:8080/cgi-bin/hello.py> | CGI script |
| <http://127.0.0.1:8080/session> | Session/cookie demo (bonus) |
| <http://127.0.0.2:8081/> | Second site on another interface |

Stop the server with `Ctrl-C`; it shuts down cleanly and frees everything.

### Configuration file

The syntax follows the `server` block of an nginx configuration.

```nginx
server {
    listen        127.0.0.1:8080;   # interface:port, or just a port
    server_name   localhost;        # used for virtual hosting
    root          ./www;            # document root
    index         index.html;       # file served for a directory
    autoindex     off;              # default for locations below
    client_max_body_size 1m;        # accepts k / m / g suffixes; 0 = unlimited

    error_page 404 /errors/404.html;

    location /upload {
        methods GET POST DELETE;    # allowed methods for this route
        upload_store ./www/uploads; # where uploaded files are written
        autoindex on;               # overrides the server-level default
        client_max_body_size 10m;   # overrides the server-wide limit
        error_page 404 /errors/upload-404.html;   # route-specific error page
    }

    location /old {
        return 301 /;               # HTTP redirect
    }

    location /cgi-bin {
        methods GET POST;
        root ./cgi-bin;
        cgi .py /usr/bin/python3;   # extension -> interpreter
    }
}
```

`root`, `index`, `autoindex`, `client_max_body_size` and `error_page` may appear
at both levels: a location that omits one inherits the server's value, and one
that declares it wins. `listen` and `server_name` are server-only; `methods`,
`return`, `cgi` and `upload_store` are location-only.

Directives are validated at startup. Anything wrong — an unknown directive, a
duplicated one, a port out of range, a missing directory, an unsupported method
— aborts the launch with the offending file and line number:

```
[ERROR] conf/invalid/invalid_port.conf:3: port out of range: '-5'
```

Ready-made configurations live in `conf/`:

- `conf/default.conf` — the full demo described above
- `conf/valid/` — minimal, multi-site and CGI-error setups
- `conf/invalid/` — 26 broken files, one per validation rule, each of which must
  be rejected at startup

### Sessions and cookies (bonus)

`GET /session` returns a page with a visit counter. On the first request the
server creates a session, keeps its state in memory and sends the identifier as
a cookie; later requests carry the cookie back and the counter goes up. Idle
sessions are garbage-collected after their TTL expires.

```bash
curl -c jar.txt http://127.0.0.1:8080/session   # visit 1, sets the cookie
curl -b jar.txt http://127.0.0.1:8080/session   # visit 2
```

In a browser, open the Network tab and watch the `Set-Cookie` response header
followed by the `Cookie` request header on the reload.

### Tests

```bash
make test                                        # curl smoke suite
tests/scripts/run-siege.sh                       # stress test (needs siege)
tests/scripts/run-valgrind.sh conf/default.conf  # memory and fd leak check
```

The server has been verified with `valgrind --leak-check=full --track-fds=yes`
(no leaked bytes, no leaked descriptors) and with `siege -b` (availability above
99.5%, stable memory and descriptor count over long runs).

## Resources

### HTTP and CGI

- [RFC 7230 — HTTP/1.1: Message Syntax and Routing](https://datatracker.ietf.org/doc/html/rfc7230)
- [RFC 7231 — HTTP/1.1: Semantics and Content](https://datatracker.ietf.org/doc/html/rfc7231)
- [RFC 3875 — The Common Gateway Interface (CGI) Version 1.1](https://datatracker.ietf.org/doc/html/rfc3875)
- [RFC 6265 — HTTP State Management Mechanism (cookies)](https://datatracker.ietf.org/doc/html/rfc6265)
- [MDN — HTTP reference](https://developer.mozilla.org/en-US/docs/Web/HTTP)
- [HTTP status codes](https://developer.mozilla.org/en-US/docs/Web/HTTP/Status)

### Sockets and event-driven I/O

- `man 2 poll`, `man 2 socket`, `man 7 socket`, `man 2 fcntl`
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [The C10K problem](http://www.kegel.com/c10k.html) — why one thread and one
  `poll()` scale better than one thread per client
- [nginx documentation](https://nginx.org/en/docs/) — used as the reference for
  configuration syntax and response behaviour

### Internal documentation

- [`docs/architecture.md`](docs/architecture.md) — module overview
- [`docs/architecture-detailed.md`](docs/architecture-detailed.md) — full
  walkthrough of every class
- [`docs/testing.md`](docs/testing.md) — test strategy

### Use of AI

AI assistance (Claude) was used on this project for the following tasks:

- **Project scaffolding** — generating the initial class and directory layout
  (`IPollable`/`EventLoop` reactor split, module boundaries) from our design
  notes, which we then reviewed and adjusted.
- **Code review** — auditing the codebase against the subject's constraints
  (single `poll()`, no `errno` after I/O calls, `fork()` only for CGI, C++98
  compliance) and reporting bugs, notably a request body limit that ignored the
  per-route override.
- **Test tooling** — writing the `curl`, `siege` and `valgrind` test scripts and
  the sample configuration files under `conf/`.
- **Documentation** — drafting this README and the files under `docs/`.

Every suggestion was read, tested and modified by us before being committed; the
HTTP parsing, event loop and CGI logic were designed and written by the team.
