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

void test_clask_part_unquoted_last_param() {
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
      .result = true,
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
    const std::string& if_modified_since) {
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
  clask::serve_file(resp, req, path);
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
  }

  remove(path.c_str());
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

int main() {
  subtest("test_clask_params", test_clask_params);
  subtest("test_clask_request_parse_multipart1", test_clask_request_parse_multipart1);
  subtest("test_clask_request_parse_multipart2", test_clask_request_parse_multipart2);
  subtest("test_clask_request_parse_multipart3", test_clask_request_parse_multipart3);
  subtest("test_clask_request_parse_multipart4", test_clask_request_parse_multipart4);
  subtest("test_clask_request_parse_multipart5", test_clask_request_parse_multipart5);
  subtest("test_clask_request_parse_multipart6", test_clask_request_parse_multipart6);
  subtest("test_clask_part_unquoted_last_param", test_clask_part_unquoted_last_param);
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
  subtest("test_clask_route_register_after_child", test_clask_route_register_after_child);
  subtest("test_clask_static_dir_route_match", test_clask_static_dir_route_match);
  subtest("test_clask_non_root_static_dir_route_match", test_clask_non_root_static_dir_route_match);
  subtest("test_clask_parse_listen_address", test_clask_parse_listen_address);
  subtest("test_clask_parse_route_method", test_clask_parse_route_method);
  subtest("test_clask_parse_path_segment", test_clask_parse_path_segment);
  subtest("test_clask_request_read_result_helpers", test_clask_request_read_result_helpers);
  subtest("test_clask_parse_content_length", test_clask_parse_content_length);
  subtest("test_clask_read_request_invalid_content_length", test_clask_read_request_invalid_content_length);
  subtest("test_clask_read_request_content_length_bounds_body", test_clask_read_request_content_length_bounds_body);
  subtest("test_clask_serve_file_if_modified_since", test_clask_serve_file_if_modified_since);
  subtest("test_clask_sse_writer_output", test_clask_sse_writer_output);
  subtest("test_clask_parent_reference_guard", test_clask_parent_reference_guard);
  subtest("test_clask_accept_failure_does_not_throw", test_clask_accept_failure_does_not_throw);
  subtest("test_clask_server_runtime_helpers", test_clask_server_runtime_helpers);
  subtest("test_clask_fluent_server_setup", test_clask_fluent_server_setup);
  subtest("test_clask_static_path_resolution", test_clask_static_path_resolution);
  return done_testing();
}
