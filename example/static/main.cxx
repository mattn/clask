#include <clask/core.hpp>

int main() {
  auto s = clask::server();
  s.log.default_level = clask::INFO;
  s.static_dir("/", "./public");
  s.GET("/api", [](clask::response_writer& resp, clask::request& req) {
    resp.set_header("content-type", "application/json");
    time_t t;
    struct tm* tm;
    time(&t);
    tm = localtime(&t);
    char buf[32];
    strftime(buf,sizeof(buf),"{\"time\":\"%Y/%m/%d %H:%M:%S\"}",tm);
    resp.write(buf);
    resp.end();
  });
  s.run();
}
