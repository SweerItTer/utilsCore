# NET Rewrite Design (utilsCore)

**Date:** 2026-02-22  
**Owner:** SweerItTer

## Goal

Rewrite `utilsCore` NET module into a reliable foundation for “PC ↔ board” communication over TCP/Wi‑Fi:

- Correct stream handling (fragmentation/coalescing)
- Keep-alive friendly (multiple requests per connection)
- Callback-driven developer experience (register handlers)
- Protocol extensibility (HTTP baseline + “small workshop” custom protocols)
- RAII everywhere (fd/threads/lifecycle)
- High-performance content transfer for files (prefer `sendfile()`; fallback to copy)
- DMABUF content transfer as raw bytes (initially allow copy path; optimize later)

## Non-goals (v1)

- Perfect RFC coverage (HTTP chunked, multipart, etc.)
- TLS/HTTPS
- WebSocket
- Zero-copy DMABUF → NIC across all platforms (hardware-specific; will fallback)

## Current Pain Points (legacy NET)

- Treats a single `recv()` as a complete message and clears the buffer → breaks HTTP and any stream protocol.
- Blocking `recv()` makes `stop()` / `join()` unreliable.
- `send()` not `sendAll()` → large responses can be partially sent.
- Per-connection thread model is heavier than needed.

## Proposed Architecture (v1)

**Threading**

- **1 IO thread** (Reactor): `epoll` handles accept/read/write, maintains connection states.
- **Worker thread pool** (`asyncThreadPool`): runs user callbacks/handlers; posts responses back to IO thread.

**Core objects**

- `utils::net::Server`: owns epoll loop, listener socket, wake fd, connection map, routers.
- `utils::net::Connection`: owns client socket fd + buffers + per-connection protocol instance.
- `utils::net::Protocol` interface: stream framing + request parsing, producing *requests*.
- `utils::net::Router`/callbacks: developer registers handlers, returns `Response`.
- `utils::net::Response`: a transport-agnostic send plan:
  - bytes
  - file-fd body (prefer `sendfile()` to socket)
  - dmabuf body (raw bytes; fallback to map+send)

**Protocol strategy**

- Default is **sniff** on first bytes: “looks like HTTP request line” → HTTP protocol, else line protocol.
- Developers can override protocol factory to install custom protocols.

## Developer Experience (target usage)

- Callback registration (line protocol): `server.line().on("PING", handler)`
- Callback registration (HTTP): `server.http().get("/path", handler)` and `staticDir("/static/", "www")`
- “No IO code in business”: handlers return a `Response` (text/bytes/file/dmabuf)

## File/DMABUF Transfer Semantics

- File transfer is “send file **content** over TCP”:
  - Primary path: `sendfile(sock, fileFd, ...)`
  - Fallback: `pread` + `sendAll`
- DMABUF transfer is “send DMABUF **content** as raw bytes”:
  - Initial implementation: `DmaBuffer::scopedMap()` + `sendAll`
  - Future: platform-specific fast paths where possible

## Stop/Shutdown Semantics

- `Server::stop()` must:
  - wake epoll (`eventfd`)
  - close listener
  - close all client sockets
  - join IO thread
  - drain worker tasks safely (RAII)

## Outputs

- New NET public API in `include/utils/net/*` (breaking change allowed).
- Updated `TCP_Test` example to use NET module (HTTP static file server with keep-alive).
- Add `docs/net-rewrite.md` with:
  - class relationship diagram
  - thread model diagram
  - migration notes / examples

