#include "clask/core.hpp"

int main() {
  auto s = clask::server();
  s.GET("/", [](clask::request& req) {
    return "OK!";
  });
  s.GET("/notfound", [](clask::response& resp, clask::request& req) {
    resp.code = 404;
    resp.set_header("content-type", "text/html");
    resp.write("Not Found");
  });
  s.GET("/foo", [](clask::response& resp, clask::request& req) {
    resp.set_header("content-type", "text/html");
    resp.write("he<b>l</b>lo");
  });
  s.run();
}
