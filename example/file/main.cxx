#include <clask/core.hpp>
#include <argparse/argparse.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
  argparse::ArgumentParser program("file", "0.0.0");
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
  program.parse_args(argc, argv);

  auto s = clask::server();
  s.log.default_level = clask::log_level::INFO;
  s.static_dir("/", program.get<std::string>("-dir"));

  auto addr = program.get<std::string>("-addr");
  std::cerr << "started " << addr << std::endl;
  s.run(addr);
}
