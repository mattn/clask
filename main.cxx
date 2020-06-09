#include <clask/core.hpp>
#include <inja.hpp>

int main() {
  auto s = clask::server();
  s.log.default_level = clask::INFO;
  s.GET("/", [](clask::request& req) {
    return "OK!";
  });
  s.GET("/notfound", [](clask::response_writer& resp, clask::request& req) {
    resp.code = 404;
    resp.set_header("content-type", "text/html");
    resp.write("Not Found");
  });
  s.GET("/foo", [](clask::response_writer& resp, clask::request& req) {
    resp.set_header("content-type", "text/html");
    resp.write("he<b>l</b>lo");
  });
  s.GET("/bar", [](clask::request& req) -> clask::response {
    return clask::response {
      .code = 200,
      .content = "hello",
      .headers = {
        { "X-Host", "hogehoge" },
      },
    };
  });

  inja::Environment env;
  inja::Template temp = env.parse_template("./index.html");
  s.GET("/app", [&](clask::request& req) -> clask::response {
    nlohmann::json data;
    data["name"] = "world";
    return clask::response {
      .code = 200,
      .content = env.render(temp, data),
    };
  });
  s.run();
}
