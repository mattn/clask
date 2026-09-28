#include <picotest/picotest.h>
#undef ok
#define CLASK_TEST
#include <clask/core.hpp>
#include <unordered_map>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef _WIN32
# ifndef SHUT_WR
#  define SHUT_WR SD_SEND
# endif
#endif

// Create a connected pair of sockets, portable across POSIX and Winsock.
// Winsock has no socketpair(), so emulate it over a loopback TCP connection.
static bool make_socket_pair(int fds[2]) {
#ifdef _WIN32
  clask::initialize_network_runtime();
  SOCKET listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener == INVALID_SOCKET) return false;
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  int addrlen = sizeof(addr);
  if (bind(listener, (sockaddr*) &addr, addrlen) != 0 ||
      getsockname(listener, (sockaddr*) &addr, &addrlen) != 0 ||
      listen(listener, 1) != 0) {
    closesocket(listener);
    return false;
  }
  SOCKET client = ::socket(AF_INET, SOCK_STREAM, 0);
  if (client == INVALID_SOCKET) {
    closesocket(listener);
    return false;
  }
  if (connect(client, (sockaddr*) &addr, addrlen) != 0) {
    closesocket(listener);
    closesocket(client);
    return false;
  }
  SOCKET server = accept(listener, nullptr, nullptr);
  closesocket(listener);
  if (server == INVALID_SOCKET) {
    closesocket(client);
    return false;
  }
  fds[0] = (int) client;
  fds[1] = (int) server;
  return true;
#else
  return socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0;
#endif
}

// Write to a socket fd portably (Winsock has no write() for sockets).
static ssize_t socket_write(int fd, const void* buf, size_t n) {
#ifdef _WIN32
  return send((SOCKET) fd, (const char*) buf, (int) n, 0);
#else
  return write(fd, buf, n);
#endif
}

void test_clask_params() {
  std::unordered_map<std::string, std::string> result;
  result = clask::params("foo");
  _ok(result.size() == 0, R"(result.size() == 0)");

  result = clask::params("foo=bar");
  _ok(result.size() == 1, R"(result.size() == 1)");
  _ok(result["foo"] == "bar", R"(result["foo"] == "bar")");

  result = clask::params("foo=bar&bar=baz");
  _ok(result.size() == 2, R"(result.size() == 2)");
  _ok(result["foo"] == "bar", R"(result["foo"] == "bar")");
  _ok(result["bar"] == "baz", R"(result["bar"] == "baz")");

  result = clask::params("hello%20world=good%2Fday");
  _ok(result.size() == 1, R"(result.size() == 1)");
  _ok(result["hello world"] == "good/day", R"(result["hello world"] == "good/day")");

  result = clask::params("greeting=hello+world&plus=1%2B2");
  _ok(result.size() == 2, R"(result.size() == 2)");
  _ok(result["greeting"] == "hello world", R"(result["greeting"] == "hello world")");
  _ok(result["plus"] == "1+2", R"(result["plus"] == "1+2")");
}

void test_clask_empty_parameters() {
  auto values = clask::params("empty=&value=first&value=&plus%2B=&equals=a=b");
  _ok(values.count("empty") == 1, "empty values are retained");
  _ok(values.at("value").empty(), "empty duplicate replaces previous value");
  _ok(values.count("plus+") == 1, "empty values still decode their keys");
  _ok(values.at("equals") == "a=b", "only the first equals separates the value");

  int fds[2];
  if (!make_socket_pair(fds)) {
    _ok(false, "create socket pair");
    return;
  }
  const std::string wire = "GET /?empty=&value=first&value=&plus%2B= HTTP/1.1\r\nHost: localhost\r\n\r\n";
  _ok(socket_write(fds[0], wire.data(), wire.size()) == (ssize_t) wire.size(), "write request");
  shutdown(fds[0], SHUT_WR);
  auto result = clask::read_request_from_socket(fds[1]);
  _ok(result.ok, "read query parameters");
  if (result.req) {
    const auto& query = result.req->uri_params;
    _ok(query.count("empty") == 1, "empty query values are retained");
    _ok(query.at("value").empty(), "empty duplicate query replaces previous value");
    _ok(query.count("plus+") == 1, "empty query values still decode their keys");
  }
  closesocket(fds[0]);
  closesocket(fds[1]);
}

void test_clask_request_parse_multipart1() {
  std::vector<clask::part> parts;
  bool result;

  parts.clear();
  clask::request req(
      "GET",
      "/",
      "/",
      {},
      {},
      "");
  result = req.parse_multipart(parts);
  _ok(result == false, R"(result == false)");
}

void test_clask_request_parse_multipart2() {
  std::vector<clask::part> parts;
  bool result;

  parts.clear();
  clask::request req(
      "GET",
      "/",
      "/",
      {},
      {
        { "Content-Type", R"(multipart/form-data;boundary="boundary")" },
      },
      "--boundary\r\n"
      "Content-Disposition: form-data; name=\"field1\"\r\n"
      "\r\n"
      "value1\r\n"
      "--boundary--\r\n");
  result = req.parse_multipart(parts);
  _ok(result == true, R"(result == true)");
  _ok(parts.size() == 1, R"(parts.size() == 1)");
  _ok(parts[0].name() == "field1", R"(parts[0].name() == "field1")");
  _ok(parts[0].body == "value1", R"(parts[0].body == "value1")");
}

void test_clask_request_parse_multipart3() {
  std::vector<clask::part> parts;
  bool result;

  parts.clear();
  clask::request req(
      "GET",
      "/",
      "/",
      {},
      {
        { "Content-Type", R"(multipart/form-data;boundary="boundary")" },
      },
      "--boundary\r\n"
      "Content-Disposition: form-data; filename=README.md; name=\"field1\"\r\n"
      "\r\n"
      "value1\r\n"
      "--boundary--\r\n");
  result = req.parse_multipart(parts);
  _ok(result == true, R"(result == true)");
  _ok(parts.size() == 1, R"(parts.size() == 1)");
  _ok(parts[0].name() == "field1", R"(parts[0].name() == "field1")");
  _ok(parts[0].filename() == "README.md", R"(parts[0].filename() == "README.md")");
  _ok(parts[0].body == "value1", R"(parts[0].body == "value1")");
}

void test_clask_request_parse_multipart5() {
  std::vector<clask::part> parts;
  bool result;

  parts.clear();
  clask::request req(
      "GET",
      "/",
      "/",
      {},
      {
        { "Content-Type", R"(multipart/form-data;boundary="boundary")" },
      },
      "--boundary\r\n"
      "Content-Disposition: form-data; name=\"field1\"\r\n"
      "\r\n"
      "value1\r\n"
      "--boundary\r\n"
      "Content-Disposition: form-data; name=\"field2\"\r\n"
      "\r\n"
      "value2\r\n"
      "--boundary--\r\n");
  result = req.parse_multipart(parts);
  _ok(result == true, R"(result == true)");
  _ok(parts.size() == 2, R"(parts.size() == 2)");
  _ok(parts[0].name() == "field1", R"(parts[0].name() == "field1")");
  _ok(parts[0].body == "value1", R"(parts[0].body == "value1")");
  _ok(parts[1].name() == "field2", R"(parts[1].name() == "field2")");
  _ok(parts[1].body == "value2", R"(parts[1].body == "value2")");
}

void test_clask_request_parse_multipart4() {
  std::vector<clask::part> parts;
  bool result;

  parts.clear();
  clask::request req(
      "GET",
      "/",
      "/",
      {},
      {
        { "Content-Type", R"(multipart/form-data;boundary="boundary")" },
      },
      "--boundary\r\n"
      "Content-Disposition: form-data; filename=README.md name=\"field1\"\r\n"
      "\r\n"
      "value1\r\n"
      "--boundary--\r\n");
  result = req.parse_multipart(parts);
  _ok(result == true, R"(result == true)");
  _ok(parts.size() == 1, R"(parts.size() == 1)");
  _ok(parts[0].name() == "", R"(parts[0].name() == "")");
  _ok(parts[0].filename() == "README.md name=\"field1", R"(parts[0].filename() == "README.md name=\"field1")");
}

void test_clask_request_parse_multipart6() {
  std::vector<clask::part> parts;
  bool result;

  parts.clear();
  clask::request req(
      "GET",
      "/",
      "/",
      {},
      {
        { "Content-Type", "multipart/form-data" },
      },
      "--boundary\r\n"
      "Content-Disposition: form-data; name=\"field1\"\r\n"
      "\r\n"
      "value1\r\n"
      "--boundary--\r\n");
  result = req.parse_multipart(parts);
  _ok(result == false, R"(result == false)");
}

void test_clask_multipart_header_case() {
  for (const auto& disposition : {"content-disposition", "CONTENT-DISPOSITION", "cOnTeNt-DiSpOsItIoN"}) {
    clask::request req("POST", "/", "/", {},
        {{"Content-Type", "multipart/form-data; boundary=boundary"}},
        std::string("--boundary\r\n") + disposition +
        ": form-data; name=\"Upload\"; filename=\"Report.TXT\"\r\n"
        "content-type: text/plain\r\n\r\nhello\r\n--boundary--\r\n");
    std::vector<clask::part> parts;
    _ok(req.parse_multipart(parts), "parse multipart with %s", disposition);
    _ok(parts.size() == 1, "one upload part");
    if (parts.size() == 1) {
      _ok(parts[0].name() == "Upload", "field name is available and preserves case");
      _ok(parts[0].filename() == "Report.TXT", "filename is available and preserves case");
      _ok(parts[0].header_value("CONTENT-TYPE") == "text/plain", "part header lookup ignores case");
      _ok(parts[0].body == "hello", "upload body is preserved");
    }
  }
}

void test_clask_part_unquoted_last_param() {
  for (const auto& encoding : {"UTF-8", "utf-8", "UtF-8"}) {
    clask::part p;
    p.headers.emplace_back("Content-Disposition",
        std::string("form-data; filename*=") + encoding + "''My%20Report.TXT");
    _ok(p.filename() == "My Report.TXT", "preserve filename case with charset %s", encoding);
  }
  {
    clask::part p;
    p.headers.emplace_back("Content-Disposition", "form-data; name=field1");
    _ok(p.name() == "field1", R"(p.name() == "field1")");
  }
  {
    clask::part p;
    p.headers.emplace_back("Content-Disposition", "form-data; filename=a.txt");
    _ok(p.filename() == "a.txt", R"(p.filename() == "a.txt")");
  }
  {
    clask::part p;
    p.headers.emplace_back("Content-Disposition", "form-data; name=field1; filename=a.txt");
    _ok(p.name() == "field1", R"(p.name() == "field1")");
    _ok(p.filename() == "a.txt", R"(p.filename() == "a.txt")");
  }
}

void test_clask_to_wstring() {
  _ok(clask::to_wstring("あいうえお") == L"あいうえお", R"(clask::to_wstring("あいうえお") == L"あいうえお")");
  _ok(
      clask::to_wstring("a\xF0\xA0\xAE\xB7z") == L"a\U00020BB7z",
      R"(clask::to_wstring("a\xF0\xA0\xAE\xB7z") == L"a\U00020BB7z")");
}

void test_clask_trim_string() {
  {
    std::string value = "  hello \t";
    clask::trim_string(value);
    _ok(value == "hello", R"(value == "hello")");
  }
  {
    std::string value = " \t\r\n";
    clask::trim_string(value);
    _ok(value == "", R"(value == "")");
  }
}

void test_clask_url_encode() {
  _ok(clask::url_encode("hello world") == "hello%20world", "space is encoded");
  _ok(clask::url_encode("/files/name", false) == "/files/name", R"(clask::url_encode("/files/name", false) == "/files/name")");
  _ok(clask::url_encode("あ") == "%E3%81%82", "utf-8 bytes are encoded");
}

void test_clask_url_decode() {
  _ok(clask::url_decode("hello%20world") == "hello world", "escaped space is decoded");
  _ok(clask::url_decode("%あ") == "%あ", "non-hex escape is preserved");
  for (const auto& suffix : {"a", "%", "%2", "%GG", "%2f", "%00"}) {
    std::string input(4096, 'a');
    input += suffix;
    input.shrink_to_fit();
    std::string expected(4096, 'a');
    if (std::string(suffix) == "%2f") expected += '/';
    else if (std::string(suffix) == "%00") expected += '\0';
    else expected += suffix;
    _ok(clask::url_decode(input) == expected, "decode suffix %s within bounds", suffix);
  }
  _ok(clask::url_decode("").empty(), "decode empty string");
  const std::string binary("a\0b%20c", 7);
  _ok(clask::url_decode(binary) == std::string("a\0b c", 5), "preserve embedded null bytes");
}

void test_clask_request_cookie_value() {
  {
    clask::request req(
        "GET",
        "/admin/dashboard",
        "/admin/dashboard",
        {},
        {
          { "Cookie", "session=abc123; path=/admin" },
        },
        "");
    _ok(req.cookie_value("session") == "abc123", R"(req.cookie_value("session") == "abc123")");
  }
  {
    clask::request req(
        "GET",
        "/public",
        "/public",
        {},
        {
          { "Cookie", "session=abc123; path=/admin" },
        },
        "");
    _ok(req.cookie_value("session") == "", R"(req.cookie_value("session") == "")");
  }
  {
    clask::request req(
        "GET",
        "/",
        "/",
        {},
        {
          { "Cookie", "token=YWJjZGVmZw==; session=a1b2" },
        },
        "");
    _ok(req.cookie_value("token") == "YWJjZGVmZw==", R"(req.cookie_value("token") == "YWJjZGVmZw==")");
    _ok(req.cookie_value("session") == "a1b2", R"(req.cookie_value("session") == "a1b2")");
  }
}

void test_clask_request_uri_param() {
  typedef struct {
    bool result;
    std::string path;
    std::vector<std::string> args;
  } test_param;
  std::vector<test_param> tests = {
    {
      .result = false,
      .path = "/foa",
      .args = {},
    },
    {
      .result = false,
      .path = "/foo",
      .args = {},
    },
    {
      .result = true,
      .path = "/foo/boo",
      .args = { "boo" },
    },
    {
      .result = true,
      .path = "/foo/ぼえ～",
      .args = { "ぼえ～" },
    },
    {
      .result = true,
      .path = "/foo/hello%20world",
      .args = { "hello world" },
    },
  };

  auto s = clask::server();
  s.GET("/foo/:bar", [](clask::request& /*req*/) -> std::string {
    return "OK";
  });
  for(auto x : tests) {
    std::vector<std::string> req_args;
    auto result = s.test_match("GET", x.path, [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
      req_args = args;
    });
    _ok(result == x.result, R"(result == x.result)");
    _ok(req_args.size() == x.args.size(), R"(req.args.size() == x.args.size())");
  }
}

void test_clask_post_route_match() {
  auto s = clask::server();
  s.POST("/submit/:id", [](clask::request& req) -> std::string {
    return req.args[0];
  });

  std::vector<std::string> req_args;
  auto result = s.test_match("POST", "/submit/42", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    req_args = args;
  });
  _ok(result == true, R"(result == true)");
  _ok(req_args.size() == 1, R"(req_args.size() == 1)");
  _ok(req_args[0] == "42", R"(req_args[0] == "42")");

  auto invalid = s.test_match("PUT", "/submit/42", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& /*args*/) {
  });
  _ok(invalid == false, R"(invalid == false)");
}

void test_clask_query_route_match() {
  auto s = clask::server();
  s.QUERY("/search/:id", [](clask::request& req) -> std::string {
    return req.args[0];
  });

  std::vector<std::string> req_args;
  auto result = s.test_match("QUERY", "/search/42", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    req_args = args;
  });
  _ok(result == true, R"(result == true)");
  _ok(req_args.size() == 1, R"(req_args.size() == 1)");
  _ok(req_args[0] == "42", R"(req_args[0] == "42")");

  auto other_method = s.test_match("GET", "/search/42", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& /*args*/) {
  });
  _ok(other_method == false, R"(other_method == false)");
}

void test_clask_root_route_match() {
  auto s = clask::server();
  s.GET("/", [](clask::request& /*req*/) -> std::string {
    return "root";
  });

  auto result = s.test_match("GET", "/", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    _ok(args.empty() == true, R"(args.empty() == true)");
  });
  _ok(result == true, R"(result == true)");

  auto miss = s.test_match("GET", "/root", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& /*args*/) {
  });
  _ok(miss == false, R"(miss == false)");
}

void test_clask_route_without_handler() {
  auto s = clask::server();
  s.GET("/foo/bar", [](clask::request&) { return "child"; });
  bool called = false;
  auto matched = s.test_match("GET", "/foo", [&](const clask::func_t&, const std::vector<std::string>&) {
    called = true;
  });
  _ok(!matched, "intermediate route without a handler does not match");
  _ok(!called, "do not dispatch an empty handler");
  s.static_dir("/", "./public", false);
  matched = s.test_match("GET", "/foo", [&](const clask::func_t& fn, const std::vector<std::string>&) {
    _ok(fn.prefix_match, "fall back to the registered static handler");
    called = true;
  });
  _ok(matched && called, "intermediate route preserves static fallback");
}

void test_clask_literal_route_priority() {
  auto s = clask::server();
  s.GET("/:id", [](clask::request& req) -> std::string {
    return req.args[0];
  });
  s.GET("/about", [](clask::request& /*req*/) -> std::string {
    return "about";
  });

  auto matched_literal = false;
  auto result = s.test_match("GET", "/about", [&](const clask::func_t& fn, const std::vector<std::string>& args) {
    clask::request req("GET", "/about", "/about", {}, {}, "");
    req.args = args;
    matched_literal = fn.f_string(req) == "about";
  });
  _ok(result == true, R"(result == true)");
  _ok(matched_literal == true, R"(matched_literal == true)");
}

void test_clask_route_register_after_child() {
  auto s = clask::server();
  s.GET("/foo/bar", [](clask::request& /*req*/) -> std::string {
    return "bar";
  });
  s.GET("/foo", [](clask::request& /*req*/) -> std::string {
    return "foo";
  });

  auto matched_foo = false;
  auto result = s.test_match("GET", "/foo", [&](const clask::func_t& fn, const std::vector<std::string>& args) {
    clask::request req("GET", "/foo", "/foo", {}, {}, "");
    req.args = args;
    matched_foo = fn.f_string != nullptr && fn.f_string(req) == "foo";
  });
  _ok(result == true, R"(result == true)");
  _ok(matched_foo == true, R"(matched_foo == true)");

  auto matched_bar = false;
  result = s.test_match("GET", "/foo/bar", [&](const clask::func_t& fn, const std::vector<std::string>& args) {
    clask::request req("GET", "/foo/bar", "/foo/bar", {}, {}, "");
    req.args = args;
    matched_bar = fn.f_string != nullptr && fn.f_string(req) == "bar";
  });
  _ok(result == true, R"(result == true)");
  _ok(matched_bar == true, R"(matched_bar == true)");
}

void test_clask_static_dir_route_match() {
  auto s = clask::server();
  s.static_dir("/", "./public");

  auto root = s.test_match("GET", "/", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    _ok(args.empty() == true, R"(args.empty() == true)");
  });
  _ok(root == true, R"(root == true)");

  auto file = s.test_match("GET", "/hello.txt", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    _ok(args.empty() == true, R"(args.empty() == true)");
  });
  _ok(file == true, R"(file == true)");

  auto nested = s.test_match("GET", "/sub/index.html", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    _ok(args.empty() == true, R"(args.empty() == true)");
  });
  _ok(nested == true, R"(nested == true)");
}

void test_clask_non_root_static_dir_route_match() {
  auto s = clask::server();
  s.static_dir("/files", "./files");

  auto root = s.test_match("GET", "/files", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    _ok(args.empty() == true, R"(args.empty() == true)");
  });
  _ok(root == true, R"(root == true)");

  auto nested = s.test_match("GET", "/files/readme.txt", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    _ok(args.empty() == true, R"(args.empty() == true)");
  });
  _ok(nested == true, R"(nested == true)");

  auto miss = s.test_match("GET", "/files-other/readme.txt", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& /*args*/) {
  });
  _ok(miss == false, R"(miss == false)");
}

void test_clask_parse_listen_address() {
  {
    auto addr = clask::parse_listen_address("127.0.0.1:8080");
    _ok(addr.host == "127.0.0.1", R"(addr.host == "127.0.0.1")");
    _ok(addr.port == 8080, R"(addr.port == 8080)");
  }
  {
    auto addr = clask::parse_listen_address(":9000");
    _ok(addr.host == "", R"(addr.host == "")");
    _ok(addr.port == 9000, R"(addr.port == 9000)");
  }
  {
    auto thrown = false;
    try {
      (void) clask::parse_listen_address("127.0.0.1");
    } catch (const std::runtime_error&) {
      thrown = true;
    }
    _ok(thrown == true, R"(thrown == true)");
  }
  {
    auto addr = clask::make_listen_address(8080);
    _ok(addr.host == "", R"(addr.host == "")");
    _ok(addr.port == 8080, R"(addr.port == 8080)");
  }
  {
    auto addr = clask::parse_listen_address("0.0.0.0:0");
    _ok(addr.host == "0.0.0.0", R"(addr.host == "0.0.0.0")");
    _ok(addr.port == 0, R"(addr.port == 0)");
  }
}

void test_clask_parse_route_method() {
  {
    auto method = clask::parse_route_method("GET");
    _ok(method.has_value() == true, R"(method.has_value() == true)");
    _ok(*method == clask::route_method::get, R"(*method == clask::route_method::get)");
  }
  {
    auto method = clask::parse_route_method("POST");
    _ok(method.has_value() == true, R"(method.has_value() == true)");
    _ok(*method == clask::route_method::post, R"(*method == clask::route_method::post)");
  }
  {
    auto method = clask::parse_route_method("QUERY");
    _ok(method.has_value() == true, R"(method.has_value() == true)");
    _ok(*method == clask::route_method::query, R"(*method == clask::route_method::query)");
  }
  {
    auto method = clask::parse_route_method("DELETE");
    _ok(method.has_value() == false, R"(method.has_value() == false)");
  }
  {
    auto method = clask::parse_route_method("get");
    _ok(method.has_value() == false, R"(method.has_value() == false)");
  }
}

void test_clask_parse_path_segment() {
  {
    auto segment = clask::parse_path_segment("/users/:id", 0);
    _ok(segment.value == "users", R"(segment.value == "users")");
    _ok(segment.next_offset == 6, R"(segment.next_offset == 6)");
    _ok(segment.placeholder == false, R"(segment.placeholder == false)");
    _ok(segment.has_more == true, R"(segment.has_more == true)");
  }
  {
    auto segment = clask::parse_path_segment("/:id", 0);
    _ok(segment.value == "id", R"(segment.value == "id")");
    _ok(segment.next_offset == 4, R"(segment.next_offset == 4)");
    _ok(segment.placeholder == true, R"(segment.placeholder == true)");
    _ok(segment.has_more == false, R"(segment.has_more == false)");
  }
  {
    auto segment = clask::parse_path_segment("/users/:id", 6);
    _ok(segment.value == "id", R"(segment.value == "id")");
    _ok(segment.next_offset == 10, R"(segment.next_offset == 10)");
    _ok(segment.placeholder == true, R"(segment.placeholder == true)");
    _ok(segment.has_more == false, R"(segment.has_more == false)");
  }
  {
    auto segment = clask::parse_path_segment("/", 0);
    _ok(segment.value == "", R"(segment.value == "")");
    _ok(segment.next_offset == 1, R"(segment.next_offset == 1)");
    _ok(segment.placeholder == false, R"(segment.placeholder == false)");
    _ok(segment.has_more == false, R"(segment.has_more == false)");
  }
  {
    auto segment = clask::parse_path_segment("/users//name", 6);
    _ok(segment.value == "", R"(segment.value == "")");
    _ok(segment.next_offset == 7, R"(segment.next_offset == 7)");
    _ok(segment.placeholder == false, R"(segment.placeholder == false)");
    _ok(segment.has_more == true, R"(segment.has_more == true)");
  }
}

void test_clask_request_read_result_helpers() {
  {
    auto result = clask::make_request_read_error(400, "Bad Request", "Invalid Request");
    _ok(result.ok == false, R"(result.ok == false)");
    _ok(result.keep_alive == false, R"(result.keep_alive == false)");
    _ok(result.error_code == 400, R"(result.error_code == 400)");
    _ok(std::string(result.error_reason) == "Bad Request", R"(std::string(result.error_reason) == "Bad Request")");
    _ok(std::string(result.error_body) == "Invalid Request", R"(std::string(result.error_body) == "Invalid Request")");
    _ok(result.req.has_value() == false, R"(result.req.has_value() == false)");
  }
  {
    auto result = clask::make_request_read_success(
        true,
        clask::request("GET", "/x", "/x", {}, {}, ""));
    _ok(result.ok == true, R"(result.ok == true)");
    _ok(result.keep_alive == true, R"(result.keep_alive == true)");
    _ok(result.error_code == 0, R"(result.error_code == 0)");
    _ok(result.req.has_value() == true, R"(result.req.has_value() == true)");
    _ok(result.req->uri == "/x", R"(result.req->uri == "/x")");
  }
}

void test_clask_parse_content_length() {
  {
    auto result = clask::parse_content_length("123");
    _ok(result.has_value() == true, R"(result.has_value() == true)");
    _ok(*result == 123, R"(*result == 123)");
  }
  _ok(clask::parse_content_length("").has_value() == false, R"(clask::parse_content_length("").has_value() == false)");
  _ok(clask::parse_content_length("-1").has_value() == false, R"(clask::parse_content_length("-1").has_value() == false)");
  _ok(clask::parse_content_length("abc").has_value() == false, R"(clask::parse_content_length("abc").has_value() == false)");
  _ok(clask::parse_content_length("12x").has_value() == false, R"(clask::parse_content_length("12x").has_value() == false)");
  _ok(clask::parse_content_length("+123").has_value() == false, R"(clask::parse_content_length("+123").has_value() == false)");
  _ok(clask::parse_content_length(" 123").has_value() == false, R"(clask::parse_content_length(" 123").has_value() == false)");
}

void test_clask_read_request_conflicting_content_length() {
  int fds[2];
  auto socket_result = make_socket_pair(fds);
  _ok(socket_result == true, R"(socket_result == true)");

  const std::string request =
      "POST / HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 3\r\n"
      "Content-Length: 10\r\n"
      "\r\n"
      "abc";
  auto written = socket_write(fds[0], request.data(), request.size());
  _ok(written == (ssize_t) request.size(), R"(written == (ssize_t) request.size())");
  shutdown(fds[0], SHUT_WR);

  auto result = clask::read_request_from_socket(fds[1]);
  _ok(result.ok == false, R"(result.ok == false)");
  _ok(result.error_code == 400, R"(result.error_code == 400)");

  closesocket(fds[0]);
  closesocket(fds[1]);
}

void test_clask_read_request_transfer_encoding() {
  for (const auto& headers : {
      "Transfer-Encoding: chunked\r\n",
      "Transfer-Encoding: gzip\r\n",
      "Transfer-Encoding: chunked\r\nContent-Length: 3\r\n",
      "Content-Length: 3\r\nTransfer-Encoding: chunked\r\n"}) {
    int fds[2];
    if (!make_socket_pair(fds)) {
      _ok(false, "create socket pair");
      return;
    }
    const auto wire = std::string("POST / HTTP/1.1\r\nHost: localhost\r\n") +
        headers + "\r\n3\r\nabc\r\n0\r\n\r\n";
    _ok(socket_write(fds[0], wire.data(), wire.size()) == (ssize_t) wire.size(), "write encoded request");
    shutdown(fds[0], SHUT_WR);
    auto result = clask::read_request_from_socket(fds[1]);
    _ok(!result.ok && !result.keep_alive, "reject unsupported framing and close connection");
    const int expected = std::string(headers).find("Content-Length") != std::string::npos ? 400 : 501;
    _ok(result.error_code == expected, "return %d for %s", expected, headers);
    closesocket(fds[0]);
    closesocket(fds[1]);
  }
}

void test_clask_read_request_invalid_content_length() {
  int fds[2];
  auto socket_result = make_socket_pair(fds);
  _ok(socket_result == true, R"(socket_result == true)");

  const std::string request =
      "POST / HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: abc\r\n"
      "\r\n";
  auto written = socket_write(fds[0], request.data(), request.size());
  _ok(written == (ssize_t) request.size(), R"(written == (ssize_t) request.size())");
  shutdown(fds[0], SHUT_WR);

  auto result = clask::read_request_from_socket(fds[1]);
  _ok(result.ok == false, R"(result.ok == false)");
  _ok(result.error_code == 400, R"(result.error_code == 400)");
  _ok(std::string(result.error_body) == "Invalid Content-Length", R"(std::string(result.error_body) == "Invalid Content-Length")");

  closesocket(fds[0]);
  closesocket(fds[1]);
}

void test_clask_connection_tokens() {
  struct connection_case {
    const char* version;
    const char* headers;
    bool keep_alive;
  };
  const connection_case cases[] = {
    {"1.1", "Connection: keep-alive, close\r\n", false},
    {"1.1", "Connection: Upgrade, ClOsE\r\n", false},
    {"1.1", "Connection: close\r\nConnection: keep-alive\r\n", false},
    {"1.1", "Connection: keep-alive\r\nConnection: close\r\n", false},
    {"1.0", "Connection: Upgrade, Keep-Alive\r\n", true},
    {"1.0", "Connection: close, keep-alive\r\n", false},
    {"1.1", "Connection: x-close\r\n", true},
    {"1.0", "Connection: x-keep-alive\r\n", false},
    {"1.1", "", true},
    {"1.0", "", false},
  };
  for (const auto& c : cases) {
    int fds[2];
    if (!make_socket_pair(fds)) {
      _ok(false, "create socket pair");
      return;
    }
    const auto wire = std::string("GET / HTTP/") + c.version +
        "\r\nHost: localhost\r\n" + c.headers + "\r\n";
    _ok(socket_write(fds[0], wire.data(), wire.size()) == (ssize_t) wire.size(), "write request");
    shutdown(fds[0], SHUT_WR);
    auto result = clask::read_request_from_socket(fds[1]);
    _ok(result.ok, "read request with connection tokens");
    _ok(result.keep_alive == c.keep_alive, "HTTP/%s %s", c.version, c.headers);
    closesocket(fds[0]);
    closesocket(fds[1]);
  }
}

void test_clask_pipelined_requests() {
  for (int length : {-1, 0, 3, 20000}) {
    int fds[2];
    if (!make_socket_pair(fds)) {
      _ok(false, "create socket pair");
      return;
    }
    const std::string body(length > 0 ? length : 0, 'x');
    std::string wire = "POST /first HTTP/1.1\r\nHost: localhost\r\n";
    if (length >= 0) wire += "Content-Length: " + std::to_string(length) + "\r\n";
    wire += "\r\n" + body + "GET /second HTTP/1.1\r\nHost: localhost\r\n\r\n";
    _ok(socket_write(fds[0], wire.data(), wire.size()) == (ssize_t) wire.size(), "write pipelined requests");
    shutdown(fds[0], SHUT_WR);
    auto first = clask::read_request_from_socket(fds[1]);
    auto second = clask::read_request_from_socket(fds[1]);
    _ok(first.ok && first.req->uri == "/first" && first.req->body == body,
        "first request body is bounded, length %d", length);
    _ok(second.ok && second.req->uri == "/second" && second.req->body.empty(),
        "second request remains readable, length %d", length);
    closesocket(fds[0]);
    closesocket(fds[1]);
  }
}

void test_clask_read_request_content_length_bounds_body() {
  int fds[2];
  auto socket_result = make_socket_pair(fds);
  _ok(socket_result == true, R"(socket_result == true)");

  const std::string request =
      "POST / HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 3\r\n"
      "\r\n"
      "abcdef";
  auto written = socket_write(fds[0], request.data(), request.size());
  _ok(written == (ssize_t) request.size(), R"(written == (ssize_t) request.size())");
  shutdown(fds[0], SHUT_WR);

  auto result = clask::read_request_from_socket(fds[1]);
  _ok(result.ok == true, R"(result.ok == true)");
  _ok(result.req.has_value() == true, R"(result.req.has_value() == true)");
  _ok(result.req->body == "abc", R"(result.req->body == "abc")");

  closesocket(fds[0]);
  closesocket(fds[1]);
}

static std::string serve_file_with_header(
    const std::string& path,
    const std::string& if_modified_since,
    const std::vector<clask::header>& extra_headers = {}) {
  int fds[2];
  if (!make_socket_pair(fds)) {
    return "";
  }
  clask::response_writer resp(fds[1], 200);
  std::vector<clask::header> headers;
  if (!if_modified_since.empty()) {
    headers.emplace_back("If-Modified-Since", if_modified_since);
  }
  clask::request req("GET", "/f.txt", "/f.txt", {}, headers, "");
  clask::serve_file(resp, req, path, extra_headers);
  closesocket(fds[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
    out.append(buf, (size_t) n);
  }
  closesocket(fds[0]);
  return out;
}

void test_clask_serve_file_if_modified_since() {
  const std::string path = "./test_if_modified_since.txt";
  {
    std::ofstream ofs(path, std::ios::binary);
    ofs << "hello";
  }

  {
    auto out = serve_file_with_header(path, "Fri, 01 Jan 2100 00:00:00 GMT");
    _ok(out.find("HTTP/1.1 304") == 0, R"(out.find("HTTP/1.1 304") == 0)");
    _ok(
        out.size() >= 4 && out.compare(out.size() - 4, 4, "\r\n\r\n") == 0,
        R"(304 response has no body)");
  }
  {
    auto out = serve_file_with_header(path, "Mon, 01 Jan 1990 00:00:00 GMT");
    _ok(out.find("HTTP/1.1 200") == 0, R"(out.find("HTTP/1.1 200") == 0)");
    _ok(out.find("\r\n\r\nhello") != std::string::npos, R"(out.find("\r\n\r\nhello") != std::string::npos)");
  }
  {
    auto out = serve_file_with_header(path, "");
    _ok(out.find("HTTP/1.1 200") == 0, R"(out.find("HTTP/1.1 200") == 0)");
    _ok(out.find("\r\n\r\nhello") != std::string::npos, R"(out.find("\r\n\r\nhello") != std::string::npos)");
    _ok(
        out.find("Cache-Control:") == std::string::npos,
        R"(no Cache-Control unless configured)");
  }

  remove(path.c_str());
}

void test_clask_serve_file_csv_content_type() {
  const std::string path = "./test_content_type.csv";
  {
    std::ofstream ofs(path, std::ios::binary);
    ofs << "a,b\n1,2\n";
  }

  auto out = serve_file_with_header(path, "");
  _ok(out.find("HTTP/1.1 200") == 0, R"(out.find("HTTP/1.1 200") == 0)");
  _ok(
      out.find("Content-Type: text/csv; charset=utf-8\r\n") != std::string::npos,
      R"(csv served with text/csv; charset=utf-8)");

  remove(path.c_str());
}

void test_clask_head_route_match() {
  auto s = clask::server();
  s.GET("/hello", [](clask::request&) -> std::string {
    return "hello";
  });

  auto matched = s.test_match("HEAD", "/hello", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    _ok(args.empty() == true, R"(args.empty() == true)");
  });
  _ok(matched == true, R"(matched == true)");

  auto miss = s.test_match("HEAD", "/nothing", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& /*args*/) {
  });
  _ok(miss == false, R"(miss == false)");
}

void test_clask_serve_file_head_request() {
  const std::string path = "./test_head_request.txt";
  {
    std::ofstream ofs(path, std::ios::binary);
    ofs << "hello";
  }

  int fds[2];
  _ok(make_socket_pair(fds) == true, R"(make_socket_pair(fds) == true)");
  clask::response_writer resp(fds[1], 200);
  resp.head_only = true;
  clask::request req("HEAD", "/f.txt", "/f.txt", {}, {}, "");
  clask::serve_file(resp, req, path);
  closesocket(fds[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
    out.append(buf, (size_t) n);
  }
  closesocket(fds[0]);

  _ok(out.find("HTTP/1.1 200") == 0, R"(out.find("HTTP/1.1 200") == 0)");
  _ok(
      out.find("Content-Length: 5\r\n") != std::string::npos,
      R"(HEAD response keeps Content-Length of the body)");
  _ok(
      out.size() >= 4 && out.compare(out.size() - 4, 4, "\r\n\r\n") == 0,
      R"(HEAD response has no body)");

  remove(path.c_str());
}

void test_clask_response_connection_close() {
  for (const auto& value : {"close", "Keep-Alive, ClOsE", "keep-alive"}) {
    for (bool request_keep_alive : {false, true}) {
      int fds[2];
      if (!make_socket_pair(fds)) {
        _ok(false, "create socket pair");
        return;
      }
      clask::func_t fn{};
      fn.f_response = [&](clask::request&) {
        return clask::response{200, "ok", {{"Connection", value}}};
      };
      clask::request req("GET", "/", "/", {}, {}, "");
      bool keep_alive = request_keep_alive;
      fn.handle(fds[1], req, keep_alive);
      shutdown(fds[1], SHUT_WR);
      std::string output;
      char buf[1024];
      ssize_t n;
      while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) output.append(buf, (size_t) n);
      bool expected = request_keep_alive && std::string(value) == "keep-alive";
      _ok(keep_alive == expected, "response close token controls connection reuse");
      if (!expected) {
        _ok(output.find("Connection: Close\r\n") != std::string::npos,
            "response advertises closure when either side requires it");
      }
      closesocket(fds[0]);
      closesocket(fds[1]);
    }
  }
}

void test_clask_partial_response_writes() {
#ifndef _WIN32
  for (int kind = 0; kind < 4; ++kind) {
    int fds[2];
    if (!make_socket_pair(fds)) {
      _ok(false, "create socket pair");
      return;
    }
    int buffer_size = 4096;
    setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    clask::set_socket_timeout(fds[1], SO_SNDTIMEO, 500);
    const std::string body(1024 * 1024, 'x');
    std::string output;
    std::thread reader([&]() {
      // Force the first send to time out after making partial progress.
      std::this_thread::sleep_for(std::chrono::milliseconds(750));
      char buf[8192];
      ssize_t n;
      while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
        output.append(buf, (size_t) n);
      }
    });
    clask::func_t fn{};
    if (kind == 0) fn.f_string = [&](clask::request&) { return body; };
    if (kind == 1) fn.f_response = [&](clask::request&) { return clask::response{200, body, {}}; };
    if (kind == 2) fn.f_writer = [&](clask::response_writer& writer, clask::request&) {
      writer.write(body);
    };
    if (kind == 3) fn.f_writer = [&](clask::response_writer& writer, clask::request&) {
      auto bytes = body;
      writer.write(bytes.data(), bytes.size());
    };
    clask::request req("GET", "/", "/", {}, {}, "");
    bool keep_alive = false;
    fn.handle(fds[1], req, keep_alive);
    shutdown(fds[1], SHUT_WR);
    reader.join();
    auto split = output.find("\r\n\r\n");
    _ok(split != std::string::npos && output.substr(split + 4) == body,
        "send complete response after a partial write, handler %d", kind);
    closesocket(fds[0]);
    keep_alive = true;
    fn.handle(fds[1], req, keep_alive);
    _ok(!keep_alive, "failed send cannot keep the connection alive");
    closesocket(fds[1]);
  }
#endif
}

void test_clask_sse_writer_output() {
  int fds[2];
  _ok(make_socket_pair(fds) == true, R"(make_socket_pair(fds) == true)");

  {
    clask::response_writer resp(fds[1], 200);
    resp.set_header("Content-Type", "text/event-stream");
    clask::server_sent_event_writer sse(resp);
    sse.write("message", "hello");
    sse.end();
  }

  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
    out.append(buf, (size_t) n);
  }
  closesocket(fds[0]);

  _ok(out.find("Transfer-Encoding") == std::string::npos, R"(out.find("Transfer-Encoding") == std::string::npos)");
  _ok(
      out.find("event: message\r\ndata: hello\r\n\r\n") != std::string::npos,
      R"(out.find("event: message\r\ndata: hello\r\n\r\n") != std::string::npos)");
  closesocket(fds[1]);
}

void test_clask_chunked_writer_output() {
  int fds[2];
  if (!make_socket_pair(fds)) {
    _ok(false, "create socket pair");
    return;
  }
  clask::response_writer resp(fds[1], 200);
  clask::chunked_writer writer(resp);
  writer.write("");
  char data[] = "world";
  writer.write(data, 0);
  writer.write("hello");
  writer.write(data, 5);
  writer.end();
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
    out.append(buf, (size_t) n);
  }
  _ok(out == "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
             "5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n",
      "chunked response has framing header and exactly one terminator");
  closesocket(fds[0]);
  closesocket(fds[1]);
}

void test_clask_response_writer_end_keeps_socket_open() {
  int fds[2];
  _ok(make_socket_pair(fds) == true, R"(make_socket_pair(fds) == true)");

  {
    clask::response_writer resp(fds[1], 200);
    resp.write("hello");
    resp.end();
  }

  // end() must not close the descriptor: the connection handler closes it
  // once, and a second close here could hit an unrelated connection that
  // reused the same fd.
  int err = 0;
  socklen_t len = sizeof(err);
  auto still_open = getsockopt(fds[1], SOL_SOCKET, SO_ERROR, (char*) &err, &len) == 0;
  _ok(still_open == true, R"(still_open == true)");

  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
    out.append(buf, (size_t) n);
  }
  _ok(out.find("\r\n\r\nhello") != std::string::npos, R"(out.find("\r\n\r\nhello") != std::string::npos)");

  closesocket(fds[0]);
  closesocket(fds[1]);
}

static std::string run_static_handler(clask::server_t& s, const std::string& uri) {
  int fds[2];
  if (!make_socket_pair(fds)) {
    return "";
  }
  s.test_match("GET", uri, [&](const clask::func_t& fn, const std::vector<std::string>& args) {
    clask::response_writer resp(fds[1], 200);
    clask::request req("GET", uri, uri, {}, {}, "");
    req.args = args;
    fn.f_writer(resp, req);
  });
  closesocket(fds[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
    out.append(buf, (size_t) n);
  }
  closesocket(fds[0]);
  return out;
}

void test_clask_static_dir_custom_404_page() {
  const std::string dir = "./test_404_public";
  const std::string custom_not_found = "<h1>custom not found</h1>";
  std::filesystem::create_directory(dir);
  {
    std::ofstream ofs(dir + "/404.html", std::ios::binary);
    ofs << custom_not_found;
  }
  {
    std::ofstream ofs(dir + "/hello.txt", std::ios::binary);
    ofs << "hi";
  }
  std::filesystem::create_directory(dir + "/subdir");

  auto s = clask::server();
  s.static_dir("/", dir);

  {
    auto out = run_static_handler(s, "/missing.txt");
    _ok(out.find("HTTP/1.1 404") == 0, R"(out.find("HTTP/1.1 404") == 0)");
    _ok(
        out.find(custom_not_found) != std::string::npos,
        R"(out.find(custom_not_found) != std::string::npos)");
    _ok(out.find("text/html") != std::string::npos, R"(out.find("text/html") != std::string::npos)");
    _ok(
        out.find("Content-Length: " + std::to_string(custom_not_found.size()) + "\r\n") != std::string::npos,
        R"(out contains Content-Length of 404.html)");
  }
  {
    auto out = run_static_handler(s, "/subdir");
    _ok(out.find("HTTP/1.1 404") == 0, R"(out.find("HTTP/1.1 404") == 0)");
    _ok(
        out.find(custom_not_found) != std::string::npos,
        R"(out.find(custom_not_found) != std::string::npos)");
  }
  {
    auto out = run_static_handler(s, "/hello.txt");
    _ok(out.find("HTTP/1.1 200") == 0, R"(out.find("HTTP/1.1 200") == 0)");
    _ok(out.find("\r\n\r\nhi") != std::string::npos, R"(out.find("\r\n\r\nhi") != std::string::npos)");
  }

  std::filesystem::remove_all(dir);
}

void test_clask_static_dir_plain_404_without_page() {
  const std::string dir = "./test_404_plain";
  std::filesystem::create_directory(dir);

  auto s = clask::server();
  s.static_dir("/", dir);

  auto out = run_static_handler(s, "/missing.txt");
  _ok(out.find("HTTP/1.1 404") == 0, R"(out.find("HTTP/1.1 404") == 0)");
  _ok(out.find("Not Found") != std::string::npos, R"(out.find("Not Found") != std::string::npos)");

  std::filesystem::remove_all(dir);
}

void test_clask_static_extra_headers() {
  const std::string path = "./test_extra_headers.txt";
  {
    std::ofstream ofs(path, std::ios::binary);
    ofs << "hello";
  }

  {
    auto out = serve_file_with_header(path, "", {{"Cache-Control", "no-cache"}});
    _ok(out.find("HTTP/1.1 200") == 0, R"(out.find("HTTP/1.1 200") == 0)");
    _ok(
        out.find("Cache-Control: no-cache\r\n") != std::string::npos,
        R"(200 has configured Cache-Control)");
  }
  {
    auto out = serve_file_with_header(path, "Fri, 01 Jan 2100 00:00:00 GMT", {{"Cache-Control", "no-cache"}});
    _ok(out.find("HTTP/1.1 304") == 0, R"(out.find("HTTP/1.1 304") == 0)");
    _ok(
        out.find("Cache-Control: no-cache\r\n") != std::string::npos,
        R"(304 has configured Cache-Control)");
  }
  remove(path.c_str());

  const std::string dir = "./test_extra_headers_dir";
  std::filesystem::create_directory(dir);
  {
    std::ofstream ofs(dir + "/404.html", std::ios::binary);
    ofs << "nope";
  }
  auto s = clask::server();
  s.static_dir("/", dir, false, {{"Cache-Control", "no-cache"}});
  auto out = run_static_handler(s, "/missing.txt");
  _ok(out.find("HTTP/1.1 404") == 0, R"(out.find("HTTP/1.1 404") == 0)");
  _ok(
      out.find("Cache-Control: no-cache\r\n") != std::string::npos,
      R"(404 page has configured Cache-Control)");
  std::filesystem::remove_all(dir);

  // plain 404 (no 404.html present) keeps the configured headers too
  const std::string plain_dir = "./test_extra_headers_plain";
  std::filesystem::create_directory(plain_dir);
  auto s2 = clask::server();
  s2.static_dir("/", plain_dir, false, {{"Cache-Control", "no-cache"}});
  auto out2 = run_static_handler(s2, "/missing.txt");
  _ok(out2.find("HTTP/1.1 404") == 0, R"(out2.find("HTTP/1.1 404") == 0)");
  _ok(
      out2.find("Cache-Control: no-cache\r\n") != std::string::npos,
      R"(plain 404 has configured Cache-Control)");
  std::filesystem::remove_all(plain_dir);
}

void test_clask_parent_reference_guard() {
  _ok(clask::contains_parent_reference("../secret") == true, R"(clask::contains_parent_reference("../secret") == true)");
  _ok(clask::contains_parent_reference("safe/path") == false, R"(clask::contains_parent_reference("safe/path") == false)");
  _ok(clask::contains_parent_reference("..") == true, R"(clask::contains_parent_reference("..") == true)");
}

void test_clask_accept_failure_does_not_throw() {
  clask::server_runtime_state runtime;
  auto thrown = false;
  try {
    clask::accept_ready_connection(-1, 4, runtime);
  } catch (const std::exception&) {
    thrown = true;
  }
  _ok(thrown == false, R"(thrown == false)");
  _ok(runtime.tracked_connections.load() == 0, R"(runtime.tracked_connections.load() == 0)");
  _ok(runtime.ready_queue.empty() == true, R"(runtime.ready_queue.empty() == true)");
}

static int socket_timeout_ms(int fd, int option) {
#ifdef _WIN32
  DWORD value = 0;
#else
  timeval value{};
#endif
  socklen_t size = sizeof(value);
  if (getsockopt(fd, SOL_SOCKET, option, (char*) &value, &size) != 0) return -1;
#ifdef _WIN32
  return (int) value;
#else
  return (int) (value.tv_sec * 1000 + value.tv_usec / 1000);
#endif
}

void test_clask_accepted_socket_timeouts() {
  clask::initialize_network_runtime();
  int listener = clask::create_listening_socket("127.0.0.1", 0);
  sockaddr_in address{};
  socklen_t size = sizeof(address);
  _ok(getsockname(listener, (sockaddr*) &address, &size) == 0, "get listener address");
  int client = (int) socket(AF_INET, SOCK_STREAM, 0);
  if (connect(client, (sockaddr*) &address, size) != 0) {
    _ok(false, "connect test client");
    closesocket(client);
    closesocket(listener);
    return;
  }
  clask::server_runtime_state runtime;
  clask::accept_ready_connection(listener, 4, runtime, 1000);
  _ok(runtime.ready_queue.size() == 1, "accepted socket is queued");
  if (runtime.ready_queue.empty()) {
    closesocket(client);
    closesocket(listener);
    return;
  }
  int fd = runtime.ready_queue.front().fd;
  _ok(socket_timeout_ms(fd, SO_RCVTIMEO) == 1000, "set receive timeout on accept");
  _ok(socket_timeout_ms(fd, SO_SNDTIMEO) == 1000, "set send timeout on accept");
  clask::func_t handler{};
  handler.f_string = [](clask::request&) { return "ok"; };
  for (int i = 0; i < 2; ++i) {
    const std::string wire = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    _ok(socket_write(client, wire.data(), wire.size()) == (ssize_t) wire.size(), "send keep-alive request");
    auto keep_alive = clask::handle_connection_request(fd, "test", 5000,
        [&](const std::string&, const std::string&, const auto& callback) {
          callback(handler, {});
          return true;
        }, false);
    _ok(keep_alive, "keep connection open after response");
    _ok(socket_timeout_ms(fd, SO_RCVTIMEO) == 1000, "keep accepted receive timeout");
    _ok(socket_timeout_ms(fd, SO_SNDTIMEO) == 1000, "keep accepted send timeout");
  }
  closesocket(fd);
  closesocket(client);
  closesocket(listener);
}

void test_clask_ready_connection_batch() {
  clask::server_runtime_state runtime;
  runtime.idle_connections.emplace(10, clask::connection_state{10, "first"});
  runtime.idle_connections.emplace(11, clask::connection_state{11, "second"});
  runtime.idle_connections.emplace(12, clask::connection_state{12, "idle"});
  clask::requeue_readable_idle_connections({{10, true, false}, {11, true, false},
      {10, true, false}, {12, false, false}, {99, true, false}}, runtime);
  _ok(runtime.ready_queue.size() == 2, "queue each readable connection once");
  if (runtime.ready_queue.size() == 2) {
    _ok(runtime.ready_queue[0].remote == "first" && runtime.ready_queue[1].remote == "second",
        "preserve connection metadata and event order");
  }
  _ok(runtime.idle_connections.size() == 1 && runtime.idle_connections.count(12) == 1,
      "leave unreadable connection in the event loop");
}

void test_clask_worker_completion_wakeup() {
  clask::initialize_network_runtime();
  clask::server_runtime_state runtime;
  runtime.wakeup.open();
  int fds[2];
  if (!make_socket_pair(fds)) {
    _ok(false, "create socket pair");
    return;
  }
  // Neither the listener stand-in nor any idle connection is readable.
  // Only the worker's completion can wake this wait before its deadline.
  for (bool keep_alive : {true, false}) {
    runtime.tracked_connections = 1;
    std::thread worker([&]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      clask::complete_connection(runtime, {fds[1], "test"}, keep_alive);
    });
    auto start = std::chrono::steady_clock::now();
    auto result = clask::wait_socket_events(fds[0], {}, 1000, runtime.wakeup.fd());
    auto elapsed = std::chrono::steady_clock::now() - start;
    worker.join();
    _ok(elapsed < std::chrono::milliseconds(500), "worker completion interrupts socket wait");
    _ok(!result.server_readable && result.events.empty(), "wakeup is not a client event");
    _ok(result.worker_completed, "identify the worker wakeup separately");
    runtime.wakeup.drain();
    clask::drain_completed_connections(runtime);
    if (keep_alive) {
      _ok(runtime.idle_connections.count(fds[1]) == 1, "completed connection returns to idle monitoring");
      runtime.idle_connections.clear();
    } else {
      _ok(runtime.tracked_connections == 0, "closed connection releases capacity promptly");
    }
  }
  // Bursts must not block workers, and draining must allow a later wakeup.
  runtime.tracked_connections = 10000;
  for (int i = 0; i < 10000; ++i) {
    clask::complete_connection(runtime, {-1, "test"}, false);
  }
  runtime.wakeup.drain();
  clask::drain_completed_connections(runtime);
  _ok(runtime.tracked_connections == 0, "drain every completion in a burst");
  runtime.wakeup.notify();
  auto start = std::chrono::steady_clock::now();
  clask::wait_socket_events(fds[0], {}, 1000, runtime.wakeup.fd());
  _ok(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500),
      "notification queued before the wait is preserved");
  runtime.wakeup.drain();
  start = std::chrono::steady_clock::now();
  clask::wait_socket_events(fds[0], {}, 50, runtime.wakeup.fd());
  _ok(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(35),
      "drained notification does not cause a busy loop");
  for (int i = 0; i < 10000; ++i) runtime.wakeup.notify();
  runtime.wakeup.drain();
  _ok(true, "notification saturation does not block the producer");
  closesocket(fds[0]);
  closesocket(fds[1]);
}

void test_clask_server_runtime_helpers() {
  _ok(clask::resolve_worker_count(7) == 7, R"(clask::resolve_worker_count(7) == 7)");
  _ok(clask::resolve_accept_queue_limit(123, 7) == 123, R"(clask::resolve_accept_queue_limit(123, 7) == 123)");
  _ok(
      clask::resolve_accept_queue_limit(0, 7) == 7 * clask::accept_queue_factor,
      R"(clask::resolve_accept_queue_limit(0, 7) == 7 * clask::accept_queue_factor)");
  _ok(clask::resolve_worker_count(1) == 1, R"(clask::resolve_worker_count(1) == 1)");
  {
    auto config = clask::resolve_server_runtime_config(7, 123, 4567);
    _ok(config.worker_count == 7, R"(config.worker_count == 7)");
    _ok(config.accept_queue_limit == 123, R"(config.accept_queue_limit == 123)");
    _ok(config.socket_timeout_ms == 4567, R"(config.socket_timeout_ms == 4567)");
  }
  {
    auto config = clask::resolve_server_runtime_config(7, 0, 5000);
    _ok(config.worker_count == 7, R"(config.worker_count == 7)");
    _ok(
        config.accept_queue_limit == 7 * clask::accept_queue_factor,
        R"(config.accept_queue_limit == 7 * clask::accept_queue_factor)");
    _ok(config.socket_timeout_ms == 5000, R"(config.socket_timeout_ms == 5000)");
  }
  {
    auto config = clask::resolve_server_runtime_config(1, 0, 0);
    _ok(config.worker_count == 1, R"(config.worker_count == 1)");
    _ok(
        config.accept_queue_limit == clask::accept_queue_factor,
        R"(config.accept_queue_limit == clask::accept_queue_factor)");
    _ok(config.socket_timeout_ms == 0, R"(config.socket_timeout_ms == 0)");
  }
}

void test_clask_fluent_server_setup() {
  auto s = clask::server()
      .worker_count(8)
      .accept_queue_limit(123)
      .socket_timeout(4567);
  s.GET("/chain/:name", [](clask::request& req) -> std::string {
    return req.args[0];
  });

  std::vector<std::string> req_args;
  auto result = s.test_match("GET", "/chain/fluent", [&](const clask::func_t& /*fn*/, const std::vector<std::string>& args) {
    req_args = args;
  });
  _ok(result == true, R"(result == true)");
  _ok(req_args.size() == 1, R"(req_args.size() == 1)");
  _ok(req_args[0] == "fluent", R"(req_args[0] == "fluent")");
}

void test_clask_static_path_resolution() {
  {
    auto result = clask::resolve_static_path("/static/hello.txt", "/static/", "./public");
    _ok(result.matched == true, R"(result.matched == true)");
    _ok(result.forbidden == false, R"(result.forbidden == false)");
    _ok(result.path == "./public/hello.txt", R"(result.path == "./public/hello.txt")");
  }
  {
    auto result = clask::resolve_static_path("/other/hello.txt", "/static/", "./public");
    _ok(result.matched == false, R"(result.matched == false)");
    _ok(result.forbidden == false, R"(result.forbidden == false)");
  }
  {
    auto result = clask::resolve_static_path("/static/../secret.txt", "/static/", "./public");
    _ok(result.matched == true, R"(result.matched == true)");
    _ok(result.forbidden == true, R"(result.forbidden == true)");
  }
  {
    auto result = clask::resolve_static_path("/static/dir/", "/static/", "./public");
    _ok(result.matched == true, R"(result.matched == true)");
    _ok(result.forbidden == false, R"(result.forbidden == false)");
    _ok(result.path == "./public/dir/", R"(result.path == "./public/dir/")");
  }
  {
    auto result = clask::resolve_static_path("/static/hello%20world.txt", "/static/", "./public");
    _ok(result.matched == true, R"(result.matched == true)");
    _ok(result.forbidden == false, R"(result.forbidden == false)");
    _ok(result.path == "./public/hello world.txt", R"(result.path == "./public/hello world.txt")");
  }
  {
    auto result = clask::resolve_static_path("/static/", "/static/", "./public");
    _ok(result.matched == true, R"(result.matched == true)");
    _ok(result.forbidden == false, R"(result.forbidden == false)");
    _ok(result.path == "./public/", R"(result.path == "./public/")");
  }
  {
    auto result = clask::resolve_static_path("/staticx/file.txt", "/static/", "./public");
    _ok(result.matched == false, R"(result.matched == false)");
    _ok(result.forbidden == false, R"(result.forbidden == false)");
  }
  {
    auto result = clask::resolve_static_path("/sta", "/static/", "./public");
    _ok(result.matched == false, R"(result.matched == false)");
    _ok(result.forbidden == false, R"(result.forbidden == false)");
  }
  {
    auto result = clask::resolve_static_path("/static/%2e%2e/secret.txt", "/static/", "./public");
    _ok(result.matched == true, R"(result.matched == true)");
    _ok(result.forbidden == true, R"(result.forbidden == true)");
  }
}

void test_clask_parse_proxy_upstream() {
  {
    auto u = clask::parse_proxy_upstream("http://127.0.0.1:9000/v1/");
    _ok(u.host == "127.0.0.1", "upstream host");
    _ok(u.port == "9000", "upstream port");
    _ok(u.host_header == "127.0.0.1:9000", "upstream host header");
    _ok(u.base_path == "/v1/", "upstream base path");
  }
  {
    auto u = clask::parse_proxy_upstream("http://example.com");
    _ok(u.host == "example.com", "host without port");
    _ok(u.port == "80", "default port");
    _ok(u.base_path == "/", "default base path");
  }
  {
    auto u = clask::parse_proxy_upstream("http://[::1]:8080/api");
    _ok(u.host == "::1", "ipv6 host");
    _ok(u.port == "8080", "ipv6 port");
    _ok(u.base_path == "/api", "ipv6 base path");
  }
  for (auto bad : {"https://example.com/", "example.com", "http://:80/", "http://host:x/", "http://[::1/"}) {
    auto thrown = false;
    try {
      clask::parse_proxy_upstream(bad);
    } catch (const std::invalid_argument&) {
      thrown = true;
    }
    _ok(thrown, "rejects %s", bad);
  }
}

void test_clask_proxy_target_path() {
  auto t = clask::proxy_target_path("/api/users?id=1", "/api/", "/v1/");
  _ok(t && *t == "/v1/users?id=1", "strips mount and keeps query");
  t = clask::proxy_target_path("/api", "/api/", "/");
  _ok(t && *t == "/", "mount without trailing slash");
  t = clask::proxy_target_path("/api/users", "/api", "/v1");
  _ok(t && *t == "/v1/users", "mount without slash joins cleanly");
  t = clask::proxy_target_path("/api/a%20b", "/api/", "/");
  _ok(t && *t == "/a%20b", "keeps raw encoding");
  t = clask::proxy_target_path("/other", "/api/", "/");
  _ok(!t, "outside mount");
  t = clask::proxy_target_path("/api/../secret", "/api/", "/");
  _ok(!t, "rejects parent reference");
  t = clask::proxy_target_path("/api/%2e%2e/secret", "/api/", "/");
  _ok(!t, "rejects encoded parent reference");
  t = clask::proxy_target_path("/api/a..b", "/api/", "/");
  _ok(t && *t == "/a..b", "dots inside a segment are allowed");
}

// Accepts one connection on a loopback port, captures the request and
// answers with a canned response.
struct test_upstream {
  int fd = -1;
  int port = 0;
  std::string received;
  std::thread th;

  bool start(const std::string& response) {
    clask::initialize_network_runtime();
    fd = (int) ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addrlen = sizeof(addr);
    if (bind(fd, (sockaddr*) &addr, addrlen) != 0
        || getsockname(fd, (sockaddr*) &addr, &addrlen) != 0
        || listen(fd, 1) != 0) {
      return false;
    }
    port = ntohs(addr.sin_port);
    th = std::thread([this, response]() {
      auto c = (int) accept(fd, nullptr, nullptr);
      if (c < 0) return;
      char buf[4096];
      while (true) {
        auto eoh = received.find("\r\n\r\n");
        if (eoh != std::string::npos) {
          auto cl = received.find("Content-Length: ");
          size_t len = cl == std::string::npos ? 0 : std::stoul(received.substr(cl + 16));
          if (received.size() >= eoh + 4 + len) break;
        }
        auto n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        received.append(buf, (size_t) n);
      }
      send(c, response.data(), (int) response.size(), 0);
      closesocket(c);
    });
    return true;
  }

  ~test_upstream() {
    if (th.joinable()) th.join();
    if (fd >= 0) closesocket(fd);
  }
};

static std::string run_proxy_handler(
    clask::server_t& s, clask::request req) {
  int fds[2];
  if (!make_socket_pair(fds)) {
    return "";
  }
  auto matched = s.test_match(req.method, req.uri, [&](const clask::func_t& fn, const std::vector<std::string>& args) {
    clask::response_writer resp(fds[1], 200);
    resp.head_only = req.method == "HEAD";
    req.args = args;
    fn.f_writer(resp, req);
  });
  closesocket(fds[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fds[0], buf, sizeof(buf), 0)) > 0) {
    out.append(buf, (size_t) n);
  }
  closesocket(fds[0]);
  return matched ? out : "";
}

void test_clask_reverse_proxy_forwards_request() {
  test_upstream up;
  if (!up.start(
      "HTTP/1.1 201 Created\r\n"
      "Content-Type: application/json\r\n"
      "Set-Cookie: a=1\r\n"
      "Set-Cookie: b=2\r\n"
      "Connection: close, X-Hop\r\n"
      "X-Hop: drop\r\n"
      "Content-Length: 11\r\n\r\n"
      "{\"ok\":true}")) {
    _ok(false, "start upstream");
    return;
  }
  auto s = clask::server();
  s.reverse_proxy("/api/", "http://127.0.0.1:" + std::to_string(up.port) + "/v1/");

  clask::request req(
      "POST", "/api/items?x=1", "/api/items", {},
      {
        {"Host", "front.example"},
        {"Content-Type", "text/plain"},
        {"Content-Length", "5"},
        {"Connection", "keep-alive, X-Secret"},
        {"X-Secret", "hidden"},
        {"X-Forwarded-For", "10.0.0.1"},
        {"X-Forwarded-Host", "evil.example"},
        {"X-Forwarded-Proto", "https"},
      },
      "hello");
  req.remote_addr = "192.168.0.2";
  auto out = run_proxy_handler(s, std::move(req));
  up.th.join();

  const auto& in = up.received;
  _ok(in.find("POST /v1/items?x=1 HTTP/1.1\r\n") == 0, "request line is rewritten");
  _ok(in.find("Host: 127.0.0.1:" + std::to_string(up.port) + "\r\n") != std::string::npos, "host is upstream");
  _ok(in.find("X-Forwarded-Host: front.example\r\n") != std::string::npos, "x-forwarded-host");
  _ok(in.find("X-Forwarded-For: 10.0.0.1, 192.168.0.2\r\n") != std::string::npos, "x-forwarded-for is appended");
  _ok(in.find("X-Forwarded-Proto: http\r\n") != std::string::npos, "x-forwarded-proto");
  _ok(in.find("evil.example") == std::string::npos, "client x-forwarded-host is ignored");
  _ok(in.find("https") == std::string::npos, "client x-forwarded-proto is ignored");
  _ok(in.find("Content-Type: text/plain\r\n") != std::string::npos, "end-to-end header is kept");
  _ok(in.find("X-Secret") == std::string::npos, "connection-listed header is dropped");
  _ok(in.find("keep-alive") == std::string::npos, "client connection header is dropped");
  _ok(in.find("Connection: close\r\n") != std::string::npos, "upstream connection closes");
  _ok(in.find("Content-Length: 5\r\n") != std::string::npos, "content-length is recomputed");
  _ok(in.find("\r\n\r\nhello") != std::string::npos, "body is forwarded");

  _ok(out.find("HTTP/1.1 201 Created\r\n") == 0, "status is relayed");
  _ok(out.find("Set-Cookie: a=1\r\n") != std::string::npos, "first set-cookie");
  _ok(out.find("Set-Cookie: b=2\r\n") != std::string::npos, "second set-cookie");
  _ok(out.find("X-Hop") == std::string::npos, "upstream connection-listed header is dropped");
  _ok(out.find("Connection: Close\r\n") != std::string::npos, "client connection closes");
  _ok(out.find("\r\n\r\n{\"ok\":true}") != std::string::npos, "body is relayed");
}

void test_clask_reverse_proxy_chunked_response() {
  test_upstream up;
  if (!up.start(
      "HTTP/1.1 100 Continue\r\n\r\n"
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n\r\n"
      "5\r\nhello\r\n0\r\n\r\n")) {
    _ok(false, "start upstream");
    return;
  }
  auto s = clask::server();
  s.reverse_proxy("/", "http://127.0.0.1:" + std::to_string(up.port));
  auto out = run_proxy_handler(s, clask::request("GET", "/", "/", {}, {{"Expect", "100-continue"}}, ""));
  up.th.join();
  _ok(up.received.find("GET / HTTP/1.1\r\n") == 0, "root is forwarded");
  _ok(up.received.find("Expect") == std::string::npos, "expect is dropped");
  _ok(up.received.find("Content-Length") == std::string::npos, "no content-length for bodiless GET");
  _ok(out.find("100 Continue") == std::string::npos, "interim response is skipped");
  _ok(out.find("HTTP/1.1 200 OK\r\n") == 0, "final status");
  _ok(out.find("Transfer-Encoding: chunked\r\n") != std::string::npos, "chunked framing is kept");
  _ok(out.find("\r\n\r\n5\r\nhello\r\n0\r\n\r\n") != std::string::npos, "chunked body is relayed verbatim");
}

void test_clask_reverse_proxy_unknown_status() {
  test_upstream up;
  if (!up.start("HTTP/1.1 599 Custom\r\nContent-Length: 2\r\n\r\nok")) {
    _ok(false, "start upstream");
    return;
  }
  auto s = clask::server();
  s.reverse_proxy("/", "http://127.0.0.1:" + std::to_string(up.port));
  auto out = run_proxy_handler(s, clask::request("GET", "/", "/", {}, {}, ""));
  up.th.join();
  _ok(out.find("HTTP/1.1 599 ") == 0, "unknown status is relayed");
  _ok(out.find("\r\n\r\nok") != std::string::npos, "body of unknown status is relayed");
  _ok(clask::status_codes.count(599) == 0, "status table is not modified");
}

void test_clask_reverse_proxy_errors() {
  auto s = clask::server();
  // Bind a port without listening so connections to it are refused.
  clask::initialize_network_runtime();
  auto fd = (int) ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addrlen = sizeof(addr);
  bind(fd, (sockaddr*) &addr, addrlen);
  getsockname(fd, (sockaddr*) &addr, &addrlen);
  auto port = ntohs(addr.sin_port);
  s.reverse_proxy("/api/", "http://127.0.0.1:" + std::to_string(port) + "/");
  auto out = run_proxy_handler(s, clask::request("GET", "/api/x", "/api/x", {}, {}, ""));
  _ok(out.find("HTTP/1.1 502 Bad Gateway\r\n") == 0, "unreachable upstream is 502");
  closesocket(fd);

  out = run_proxy_handler(s, clask::request("GET", "/api/../etc", "/api/../etc", {}, {}, ""));
  _ok(out.find("HTTP/1.1 404") == 0, "parent reference is 404");

  auto matched = s.test_match("POST", "/api/x", [](const clask::func_t&, const std::vector<std::string>&) {});
  _ok(matched, "POST is routed");
  matched = s.test_match("QUERY", "/api/x", [](const clask::func_t&, const std::vector<std::string>&) {});
  _ok(matched, "QUERY is routed");
}

int main() {
  subtest("test_clask_connection_tokens", test_clask_connection_tokens);
  subtest("test_clask_empty_parameters", test_clask_empty_parameters);
  subtest("test_clask_params", test_clask_params);
  subtest("test_clask_request_parse_multipart1", test_clask_request_parse_multipart1);
  subtest("test_clask_request_parse_multipart2", test_clask_request_parse_multipart2);
  subtest("test_clask_request_parse_multipart3", test_clask_request_parse_multipart3);
  subtest("test_clask_request_parse_multipart4", test_clask_request_parse_multipart4);
  subtest("test_clask_request_parse_multipart5", test_clask_request_parse_multipart5);
  subtest("test_clask_request_parse_multipart6", test_clask_request_parse_multipart6);
  subtest("test_clask_part_unquoted_last_param", test_clask_part_unquoted_last_param);
  subtest("test_clask_multipart_header_case", test_clask_multipart_header_case);
  subtest("test_clask_to_wstring", test_clask_to_wstring);
  subtest("test_clask_trim_string", test_clask_trim_string);
  subtest("test_clask_url_encode", test_clask_url_encode);
  subtest("test_clask_url_decode", test_clask_url_decode);
  subtest("test_clask_request_cookie_value", test_clask_request_cookie_value);
  subtest("test_clask_request_uri_param", test_clask_request_uri_param);
  subtest("test_clask_post_route_match", test_clask_post_route_match);
  subtest("test_clask_query_route_match", test_clask_query_route_match);
  subtest("test_clask_root_route_match", test_clask_root_route_match);
  subtest("test_clask_literal_route_priority", test_clask_literal_route_priority);
  subtest("test_clask_route_without_handler", test_clask_route_without_handler);
  subtest("test_clask_route_register_after_child", test_clask_route_register_after_child);
  subtest("test_clask_static_dir_route_match", test_clask_static_dir_route_match);
  subtest("test_clask_non_root_static_dir_route_match", test_clask_non_root_static_dir_route_match);
  subtest("test_clask_parse_listen_address", test_clask_parse_listen_address);
  subtest("test_clask_parse_route_method", test_clask_parse_route_method);
  subtest("test_clask_parse_path_segment", test_clask_parse_path_segment);
  subtest("test_clask_request_read_result_helpers", test_clask_request_read_result_helpers);
  subtest("test_clask_parse_content_length", test_clask_parse_content_length);
  subtest("test_clask_read_request_invalid_content_length", test_clask_read_request_invalid_content_length);
  subtest("test_clask_read_request_transfer_encoding", test_clask_read_request_transfer_encoding);
  subtest("test_clask_read_request_conflicting_content_length", test_clask_read_request_conflicting_content_length);
  subtest("test_clask_read_request_content_length_bounds_body", test_clask_read_request_content_length_bounds_body);
  subtest("test_clask_pipelined_requests", test_clask_pipelined_requests);
  subtest("test_clask_serve_file_if_modified_since", test_clask_serve_file_if_modified_since);
  subtest("test_clask_serve_file_csv_content_type", test_clask_serve_file_csv_content_type);
  subtest("test_clask_head_route_match", test_clask_head_route_match);
  subtest("test_clask_serve_file_head_request", test_clask_serve_file_head_request);
  subtest("test_clask_sse_writer_output", test_clask_sse_writer_output);
  subtest("test_clask_response_connection_close", test_clask_response_connection_close);
  subtest("test_clask_partial_response_writes", test_clask_partial_response_writes);
  subtest("test_clask_chunked_writer_output", test_clask_chunked_writer_output);
  subtest("test_clask_response_writer_end_keeps_socket_open", test_clask_response_writer_end_keeps_socket_open);
  subtest("test_clask_static_dir_custom_404_page", test_clask_static_dir_custom_404_page);
  subtest("test_clask_static_dir_plain_404_without_page", test_clask_static_dir_plain_404_without_page);
  subtest("test_clask_static_extra_headers", test_clask_static_extra_headers);
  subtest("test_clask_parent_reference_guard", test_clask_parent_reference_guard);
  subtest("test_clask_accept_failure_does_not_throw", test_clask_accept_failure_does_not_throw);
  subtest("test_clask_server_runtime_helpers", test_clask_server_runtime_helpers);
  subtest("test_clask_worker_completion_wakeup", test_clask_worker_completion_wakeup);
  subtest("test_clask_accepted_socket_timeouts", test_clask_accepted_socket_timeouts);
  subtest("test_clask_ready_connection_batch", test_clask_ready_connection_batch);
  subtest("test_clask_fluent_server_setup", test_clask_fluent_server_setup);
  subtest("test_clask_static_path_resolution", test_clask_static_path_resolution);
  subtest("test_clask_parse_proxy_upstream", test_clask_parse_proxy_upstream);
  subtest("test_clask_proxy_target_path", test_clask_proxy_target_path);
  subtest("test_clask_reverse_proxy_forwards_request", test_clask_reverse_proxy_forwards_request);
  subtest("test_clask_reverse_proxy_chunked_response", test_clask_reverse_proxy_chunked_response);
  subtest("test_clask_reverse_proxy_unknown_status", test_clask_reverse_proxy_unknown_status);
  subtest("test_clask_reverse_proxy_errors", test_clask_reverse_proxy_errors);
  return done_testing();
}
