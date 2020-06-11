#include <clask/core.hpp>

int main() {
  auto s = clask::server();
  s.log.default_level = clask::INFO;
  s.GET("/foo", [](clask::response_writer& resp, clask::request& req) {
    resp.set_header("content-type", "text/html");
    resp.write("he<b>l</b>lo");
    resp.end();
  });
  s.static_dir("/", "./public");
  s.run();
}
