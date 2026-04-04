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
    auto method = clask::parse_route_method("DELETE");
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
}

int main() {
  subtest("test_clask_params", test_clask_params);
  subtest("test_clask_request_parse_multipart1", test_clask_request_parse_multipart1);
  subtest("test_clask_request_parse_multipart2", test_clask_request_parse_multipart2);
  subtest("test_clask_request_parse_multipart3", test_clask_request_parse_multipart3);
  subtest("test_clask_request_parse_multipart4", test_clask_request_parse_multipart4);
  subtest("test_clask_to_wstring", test_clask_to_wstring);
  subtest("test_clask_request_uri_param", test_clask_request_uri_param);
  subtest("test_clask_post_route_match", test_clask_post_route_match);
  subtest("test_clask_root_route_match", test_clask_root_route_match);
  subtest("test_clask_parse_listen_address", test_clask_parse_listen_address);
  subtest("test_clask_parse_route_method", test_clask_parse_route_method);
  subtest("test_clask_parse_path_segment", test_clask_parse_path_segment);
  subtest("test_clask_server_runtime_helpers", test_clask_server_runtime_helpers);
  subtest("test_clask_fluent_server_setup", test_clask_fluent_server_setup);
  subtest("test_clask_static_path_resolution", test_clask_static_path_resolution);
  return done_testing();
}
