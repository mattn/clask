#include <clask/core.hpp>

int main() {
  auto s = clask::server();
  s.log.default_level = clask::log_level::INFO;
  s.GET("/", [](clask::request& req) {
    return "OK!";
  });
  s.GET("/notfound", [](clask::response_writer& resp, clask::request& req) {
    resp.code = 404;
    resp.set_header("content-type", "text/html");
    resp.write("Not Found");
  });
  s.GET("/foo", [](clask::response_writer& resp, clask::request& req) {
    resp.set_header("content-type", "text/html");
    resp.write("he<b>l</b>lo");
    resp.end();
  });
  s.GET("/zoo/:name", [](clask::request& req) -> std::string {
    if (req.args.empty() || req.args[0].empty()) return "Wow!";
    return req.args[0];
  });
  s.GET("/download", [](clask::response_writer& resp, clask::request& req) {
    resp.set_header("content-type", "application/octet-stream");
    resp.set_header("content-disposition", "attachment; filename=README.md");
    std::ifstream is("README.md", std::ios::in | std::ios::binary);
    char buf[BUFSIZ];
    while (!is.eof()) {
      auto size = is.read(buf, sizeof(buf)).gcount();
      resp.write(buf, size);
    }
    resp.end();
  });
  s.GET("/bar", [](clask::request& req) -> clask::response {
    return clask::response {
      .code = 200,
      .content = "hello",
      .headers = {
        { "X-Host", "hogehoge" },
      },
    };
  });
  s.static_dir("/static/", "./public", true);
  s.run();
}
