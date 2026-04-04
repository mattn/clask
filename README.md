# clask

![C/C++ CI](https://github.com/mattn/clask/workflows/C/C++%20CI/badge.svg)

Very Very Experimental Web micro-framework like flask in C++.

*DO NOT USE THIS IN PRODUCTION*

## Usage

```cpp
#include "clask/core.hpp"

int main() {
  auto s = clask::server()
    .worker_count(32)
    .accept_queue_limit(4096)
    .socket_timeout(3000);
  s.GET("/", [](clask::request& req) {
    return "OK!";
  });
  s.GET("/foo", [](clask::response& resp, clask::request& req) {
    resp.set_header("content-type", "text/html");
    resp.write("he<b>l</b>lo");
  });
  s.run();
}
```

`run()` uses a worker-pool runtime by default. Accepted sockets are queued, idle keep-alive connections stay in the event loop, and overloaded accepts return `503 Service Unavailable` instead of spawning unbounded threads.

## Runtime Tuning

`server_t` exposes a few knobs for the worker-pool based runtime:

```cpp
auto s = clask::server()
  .worker_count(32)
  .accept_queue_limit(4096)
  .socket_timeout(3000);
```

- `worker_count(n)` sets the number of worker threads.
- `accept_queue_limit(n)` caps queued accepted sockets before returning `503 Service Unavailable`.
- `socket_timeout(ms)` sets socket send/receive timeout in milliseconds.

The current worker-pool runtime supports HTTP keep-alive by routing only readable sockets to workers. Idle keep-alive connections stay in the event loop instead of occupying one worker thread each.

Reasonable defaults are used when these values are left unset:

- `worker_count()` defaults to roughly `2 * hardware_concurrency()`, with a fallback of `4`.
- `accept_queue_limit()` defaults to `worker_count * 64`.
- `socket_timeout()` defaults to `5000` milliseconds.

## Runtime Notes

- This runtime is intended to stay portable across Linux and Windows.
- The socket wait loop is abstracted so platform-specific implementations can be swapped later without changing the server API.
- Keep-alive is optimized for pooled workers by only dispatching readable sockets, not by pinning one worker per connection.

## TODO

* ~~Unescape paths in request~~
* ~~Unescape query parameters in request~~
* ~~Implement keep-alive~~
* ~~Serve static file~~
* Responses oriented classes such as JSON

## License

MIT

This product contains following third-party libraries:

* picohttpparser written by kazuho

## Author

Yasuhiro Matsumoto (a.k.a. mattn)
