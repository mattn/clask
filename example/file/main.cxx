#include <clask/core.hpp>

int main() {
  auto s = clask::server();
  s.log.default_level = clask::log_level::INFO;
  s.static_dir("/", "./public");
  s.run();
}
