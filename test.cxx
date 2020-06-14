#include <picotest/picotest.h>
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

int main() {
  subtest("test_clask_params", test_clask_params);
  subtest("test_clask_request_parse_multipart1", test_clask_request_parse_multipart1);
  subtest("test_clask_request_parse_multipart2", test_clask_request_parse_multipart2);
  subtest("test_clask_request_parse_multipart3", test_clask_request_parse_multipart3);
  subtest("test_clask_request_parse_multipart4", test_clask_request_parse_multipart4);
  subtest("test_clask_to_wstring", test_clask_to_wstring);
}
