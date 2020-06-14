#include <clask/core.hpp>

static void save_file(clask::part p) {
  auto filename = p.filename();
  if (filename.empty()) return;
  std::filesystem::path fn(clask::to_wstring(filename));
  std::wstring wfn = L"download/" + fn.filename().wstring();
  std::ofstream out(wfn.c_str(), std::ios::out | std::ios::binary);
  out << p.body;
  out.close();
}

int main() {
  auto s = clask::server();
  s.log.default_level = clask::INFO;
  s.static_dir("/", "./public");
  s.POST("/upload", [](clask::request& req) -> clask::response {
    std::vector<clask::part> parts;
    if (!req.parse_multipart(parts)) {
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
