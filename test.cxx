#include <picotest/picotest.h>
#undef ok
#define CLASK_TEST
#include <clask/core.hpp>
#include <unordered_map>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

void test_clask_to_wstring() {
  _ok(clask::to_wstring("あいうえお") == L"あいうえお", R"(clask::to_wstring("あいうえお") == L"あいうえお")");
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

void test_clask_parse_listen_address() {
  {
    auto [host, port] = clask::parse_listen_address("127.0.0.1:8080");
    _ok(host == "127.0.0.1", R"(host == "127.0.0.1")");
    _ok(port == 8080, R"(port == 8080)");
  }
  {
    auto [host, port] = clask::parse_listen_address(":9000");
    _ok(host == "", R"(host == "")");
    _ok(port == 9000, R"(port == 9000)");
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
}

void test_clask_server_runtime_helpers() {
  _ok(clask::resolve_worker_count(7) == 7, R"(clask::resolve_worker_count(7) == 7)");
  _ok(clask::resolve_accept_queue_limit(123, 7) == 123, R"(clask::resolve_accept_queue_limit(123, 7) == 123)");
  _ok(
      clask::resolve_accept_queue_limit(0, 7) == 7 * clask::accept_queue_factor,
      R"(clask::resolve_accept_queue_limit(0, 7) == 7 * clask::accept_queue_factor)");
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

int main() {
  subtest("test_clask_params", test_clask_params);
  subtest("test_clask_request_parse_multipart1", test_clask_request_parse_multipart1);
  subtest("test_clask_request_parse_multipart2", test_clask_request_parse_multipart2);
  subtest("test_clask_request_parse_multipart3", test_clask_request_parse_multipart3);
  subtest("test_clask_request_parse_multipart4", test_clask_request_parse_multipart4);
  subtest("test_clask_to_wstring", test_clask_to_wstring);
  subtest("test_clask_request_uri_param", test_clask_request_uri_param);
  subtest("test_clask_parse_listen_address", test_clask_parse_listen_address);
  subtest("test_clask_server_runtime_helpers", test_clask_server_runtime_helpers);
  subtest("test_clask_fluent_server_setup", test_clask_fluent_server_setup);
  return done_testing();
}
