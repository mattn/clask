#include <clask/core.hpp>

static void save_file(clask::part p) {
  auto filename = p.filename();
  if (filename.empty()) return;
  std::filesystem::path fn(clask::to_wstring(filename));
  fn = L"files/" + fn.filename().wstring();
  std::ofstream out(fn, std::ios::out | std::ios::binary);
  out << p.body;
  out.close();
}

int main() {
  auto s = clask::server();
  s.log.default_level = clask::INFO;
  s.static_dir("/", "./public");
  s.POST("/upload", [](clask::request& req) -> clask::response {
    std::vector<clask::part> parts;
    if (!req.parse_multipart(parts) || parts.size() == 0) {
      return clask::response {
        .code = 400,
        .content = "Bad Request",
      };
    };
    for (auto part : parts) {
      save_file(part);
    }
    return clask::response {
      .code = 302,
      .content = "",
      .headers = {
        {"Location", "/"},
      },
    };
  });
  s.run();
}
