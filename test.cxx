#include <gtest/gtest.h>
#include <clask/core.hpp>
#include <unordered_map>

TEST(ClaskTest, params) {
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
