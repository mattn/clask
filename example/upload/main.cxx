#include <clask/core.hpp>
#include <nlohmann/json.hpp>

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
  s.static_dir("/files", "./files");
  s.static_dir("/", "./public");
  s.GET("/api", [](clask::request& req) {
    nlohmann::json data;
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator("files")) {
      auto fn = e.path().filename().u8string();
      if (fn[0] == '.') continue;
	  data[n++] = fn;
    }
	return data.dump();
  });
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
