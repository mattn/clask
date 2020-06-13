# clask

![C/C++ CI](https://github.com/mattn/clask/workflows/C/C++%20CI/badge.svg)

Very Very Experimental Web micro-framework like flask in C++.

*DO NOT USE THIS IN PRODUCTION*

## Usage

```cpp
#include "clask/core.hpp"

int main() {
  auto s = clask::server();
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
