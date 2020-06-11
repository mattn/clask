#include <clask/core.hpp>

int main() {
  auto s = clask::server();
  s.log.default_level = clask::INFO;
  s.static_dir("/", "./public");
  s.GET("/api", [](clask::response_writer& resp, clask::request& req) {
    resp.set_header("content-type", "application/json");
    time_t t;
    struct tm* tm;
    std::time(&t);
    tm = std::gmtime(&t);
    std::stringstream date;
    date <<  R"({"time": ")"
      << std::put_time(tm, "%a, %d %B %Y %H:%M:%S GMT")
      << R"("})";
    resp.write(date.str());
    resp.end();
  });
  s.run();
}
