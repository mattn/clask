#include <gtest/gtest.h>
#include <clask/core.hpp>
#include <unordered_map>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

TEST(clask, params) {
  std::unordered_map<std::string, std::string> result;
  result = clask::params("foo");
  ASSERT_EQ(0, result.size());

  result = clask::params("foo=bar");
  ASSERT_EQ(1, result.size());
  ASSERT_EQ("bar", result["foo"]);

  result = clask::params("foo=bar&bar=baz");
  ASSERT_EQ(2, result.size());
  ASSERT_EQ("bar", result["foo"]);
  ASSERT_EQ("baz", result["bar"]);
}

TEST(clask, request_parse_multipart) {
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
  ASSERT_EQ(false, result);
}

TEST(clask, request_parse_multipart2) {
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
  ASSERT_EQ(true, result);
  ASSERT_EQ(1, parts.size());
  ASSERT_EQ("field1", parts[0].name());
}
