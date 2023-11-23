#include <clask/core.hpp>
#include <ctime>
#include <unistd.h>

int main() {
  auto s = clask::server();

  s.GET("/", [&](clask::chunked_writer& w, clask::request& r) {
      w.code = 200;
      w.set_header("content-type", "text/event-stream; charset=utf-8");
      w.write_headers();
      for (int n = 0; n < 100; n++) {
        w.write("💩");
        usleep(100000);
      }
      w.end();
  });
  s.run();
}
