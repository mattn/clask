#include <clask/core.hpp>
#include <nlohmann/json.hpp>

int main() {
  auto s = clask::server();

  s.GET("/api", [&](clask::request&) {
      nlohmann::json data;
      data["user"][0]["id"] = 1;
      data["user"][0]["name"] = "mattn";
      data["user"][0]["created_at"] = "2020/06/14 00:41:35";
      data["user"][1]["id"] = 2;
      data["user"][1]["name"] = "gopher";
      data["user"][1]["created_at"] = "2020/06/12 15:07:54";
      return data.dump();
  });
  s.static_dir("/", "./public");
  s.run();
}
