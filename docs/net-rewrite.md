# utilsCore net v1

**Date:** 2026-03-14  
**Scope:** `include/utils/net/*`, `src/utils/net/*`, `examples/Net_Http_Demo`

## What Landed

`utils::net` now provides a config-driven HTTP service foundation for Linux:

- epoll-based TCP/HTTP server core
- JSON config loading
- static file serving
- route binding from config
- plugin loading via shared libraries
- typed C++ plugin handlers for HTTP APIs

The primary runtime entry is `utils::net::ConfiguredServer`.

## Runtime Model

1. Load `net_demo.json`-style configuration.
2. Build `ServerConfig`, static directory mappings, plugin instances, and route bindings.
3. `dlopen()` each plugin library.
4. Call the plugin factory and let it register named handlers through `HttpHandlerRegistrar`.
5. Bind configured routes like `GET /api/ping -> demo.ping`.
6. Start the epoll IO loop and worker pool.

## Plugin Contract

The plugin boundary is intentionally small:

- exported symbol `createNetPlugin`
- optional exported symbol `destroyNetPlugin`
- plugin implementation derives from `utils::net::NetPlugin`
- business logic stays in typed C++ handlers, not in configuration files

This keeps v1 fast and convenient, while explicitly requiring:

- same CPU platform
- same toolchain family
- same major dependency environment

ABI stability across arbitrary build environments is not a v1 goal.

## JSON Config Shape

```json
{
  "server": {
    "bind_address": "127.0.0.1",
    "port": 18080
  },
  "static_dirs": [
    {
      "url_prefix": "/static/",
      "directory": "www"
    }
  ],
  "plugins": [
    {
      "instance_name": "demo",
      "library_path": "plugins/net_demo_plugin.so",
      "config": {
        "download_file": "www/sample.txt"
      }
    }
  ],
  "routes": [
    {
      "method": "GET",
      "path": "/api/ping",
      "plugin": "demo",
      "handler": "ping"
    }
  ]
}
```

## Demo Endpoints

The shipped demo covers:

- `GET /api/ping`
- `POST /api/echo`
- `GET /static/index.html`
- `GET /download/sample.txt`

Example:

```bash
./Net_Http_Demo
curl http://127.0.0.1:18080/api/ping
curl -X POST http://127.0.0.1:18080/api/echo -H 'Content-Type: application/json' -d '{"message":"Hello"}'
curl http://127.0.0.1:18080/static/index.html
curl -OJ http://127.0.0.1:18080/download/sample.txt
```

## Notes

- `LineRouter` is still present in the transport layer, but v1 config and demo are HTTP-first.
- DMABUF response support remains in `Response`/`Body`, but the demo validates bytes + file download paths on a standard Linux host.
- The next stage should build board-side control services or plugins on top of this foundation, rather than hard-coding device workflows into `utilsCore`.
