#include <clask/core.hpp>

static std::wstring convert(const std::string& input) {
  try {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(input);
  } catch (std::range_error& e) {
    size_t length = input.length();
    std::wstring result;
    result.reserve(length);
    for (size_t i = 0; i < length; i++) {
      result.push_back(input[i] & 0xFF);
    }
    return result;
  }
}

static void save_file(clask::part p) {
  auto filename = p.filename();
  if (filename.empty()) return;
  std::wstring wfn = convert(filename);
  std::filesystem::path fn(wfn);
  wfn = L"download/" + fn.filename().wstring();
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
