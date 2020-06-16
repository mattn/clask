#include <clask/core.hpp>
#include <nlohmann/json.hpp>

static bool save_file(clask::part p) {
  auto filename = p.filename();
  if (filename.empty()) {
    clask::logger().get(clask::ERR) << "filename is not provided";
    return false;
  }
  std::filesystem::path fn(clask::to_wstring(filename));
  if (!fn.has_filename()) {
    clask::logger().get(clask::ERR) << "filename is not provided";
    return false;
  }
  std::wstring wfn = fn.filename().wstring();
  if (wfn.empty() || wfn[0] == '.') {
    clask::logger().get(clask::ERR) << "filename is not provided";
    return false;
  }
  fn = L"files/" + wfn;
  std::ofstream out(fn, std::ios::out | std::ios::binary);
  out << p.body;
  out.close();
  return true;
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
    if (req.header_value("expect") == "100-continue") {
      return clask::response {
        .code = 100,
      };
    }
    if (!req.parse_multipart(parts) || parts.size() == 0) {
      return clask::response {
        .code = 400,
        .content = "Bad Request",
      };
    }
    for (auto part : parts) {
      if (!save_file(part)) {
        return clask::response {
          .code = 400,
          .content = "Bad Request",
        };
      }
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
