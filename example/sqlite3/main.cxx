#include <clask/core.hpp>
#include <inja.hpp>
#include <sqlite3.h>

int main() {
  sqlite3 *db = nullptr;
  int r = sqlite3_open("bbs.db", &db);
  if (SQLITE_OK != r) {
    throw std::runtime_error("can't open database");
  }

  inja::Environment env;
  inja::Template temp = env.parse_template("./index.html");
  env.add_callback("escape", 1, [](inja::Arguments& args) {
    return clask::html_encode(args.at(0)->get<std::string>());
  });
  auto s = clask::server();

  s.GET("/", [&](clask::request& req) -> clask::response {
    nlohmann::json data;
    auto sql = "select id, text from bbs order by created";
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare(db, sql, -1, &stmt, nullptr);
    int n = 0;
    data["posts"] = {};
    while (SQLITE_DONE != sqlite3_step(stmt)) {
      data["posts"][n]["id"] = (std::string) (char*) sqlite3_column_text(stmt, 0);
      data["posts"][n]["text"] = (std::string) (char*) sqlite3_column_text(stmt, 1);
      n++;
    }
    sqlite3_finalize(stmt);
    return clask::response {
      .code = 200,
      .content = env.render(temp, data),
    };
  });

  s.POST("/post", [&](clask::request& req) -> clask::response {
    auto params = clask::params(req.body);
    auto q = params["text"];
    if (q.empty()) {
      return clask::response {
        .code = 400,
        .content = "Bad Request",
      };
    }
    auto sql = "insert into bbs(text) values(?)";
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, q.c_str(), -1,
      (sqlite3_destructor_type) SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return clask::response {
      .code = 302,
      .headers = {
        { "Location", "/" },
      },
    };
  });

  s.run();
}
