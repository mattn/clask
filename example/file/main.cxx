#include <clask/core.hpp>
#include <clask/version.h>
#include <argparse/argparse.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
  argparse::ArgumentParser program("file", CLASK_VERSION);
  program.add_argument("-dir")
      .default_value("./public")
      .help("public directory")
      .metavar("PUBLIC_DIR")
      .nargs(1);
  program.add_argument("-addr")
      .default_value("0.0.0.0:8080")
      .help("server address")
      .metavar("ADDR")
      .nargs(1);
  program.add_argument("-cache-control")
      .default_value("no-cache")
      .help("Cache-Control header for served files (empty to disable)")
      .metavar("VALUE")
      .nargs(1);
  program.parse_args(argc, argv);

  auto s = clask::server();
  s.log.default_level = clask::log_level::INFO;
  std::vector<clask::header> extra_headers;
  auto cache_control = program.get<std::string>("-cache-control");
  if (!cache_control.empty()) {
    extra_headers.emplace_back("Cache-Control", cache_control);
  }
  s.static_dir("/", program.get<std::string>("-dir"), false, extra_headers);

  auto addr = program.get<std::string>("-addr");
  std::cerr << "started " << addr << std::endl;
  s.run(addr);
}
