# clask

Very Very Experimental Web micro-framework like flask in C++.

## Usage

```cpp
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

## License

MIT

## Author

Yasuhiro Matsumoto (a.k.a. mattn)
