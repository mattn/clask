#include <clask/core.hpp>
#include <mutex>
#include <random>
#include <memory>
#include <inja.hpp>
#include <digestpp/digestpp.hpp>

typedef struct {
  int id;
  std::string username;
  std::string password_hash;
  std::string realname;
} user;

static std::vector<user> users = {
  { .id = 123, .username = "trump", .password_hash = "c69c8b8a98f71390d531d488418243ca826d798185a40fcc4f39102c9de053e5", .realname = "Donald Trump" }, // trump:MakeAmericaGreatAgain
  { .id = 234, .username = "obama", .password_hash = "d79af1e37e966bff2ac1ba2f95527b33faae9238bcff3f26d21402e712e03361", .realname = "Barack Obama" }, // obama:YesWeCan
};

static std::string hash_string(const std::string& s) {
  return digestpp::sha512(256).absorb(s).hexdigest();
}

static std::unique_ptr<user> authenticate(const std::string& username, const std::string& password) {
  auto hashed = hash_string(password);
  for (auto& u : users) {
    if (u.username == username && u.password_hash == hashed) {
      std::unique_ptr<user> p(new user);
      *p.get() = u;
      return p;
    }
  }
  return nullptr;
}

static std::string random_string() {
  std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  std::random_device rd;
  std::mt19937 generator(rd());
  std::shuffle(str.begin(), str.end(), generator);
  return str.substr(0, 32);
}

int main() {
  inja::Environment env;
  inja::Template temp = env.parse_template("./index.html");
  env.add_callback("escape", 1, [](inja::Arguments& args) {
    return clask::html_encode(args.at(0)->get<std::string>());
  });
  std::mutex mu;
  std::unordered_map<std::string, user> sessions;

  auto s = clask::server();
  s.POST("/login", [&](clask::request& req) -> clask::response {
    auto params = clask::params(req.body);
    auto username = params["username"];
    auto password = params["password"];
    auto u = authenticate(username, password);
    if (nullptr != u.get()) {
      auto r = random_string();
      mu.lock();
      sessions[r] = *u;
      mu.unlock();
      return clask::response {
        .code = 302,
        .content = "",
        .headers = {
          {"Location", "/"},
          {"Set-Cookie", "session-id=" + r + "; path=/;" },
        },
      };
    }
    return clask::response {
      .code = 302,
      .content = "",
      .headers = {
        {"Location", "/?err=auth"},
      },
    };
  });
  s.POST("/logout", [&](clask::request& req) -> clask::response {
    auto session_id = req.cookie_value("session-id");
    mu.lock();
    if (!session_id.empty() && sessions.count(session_id) > 0) {
      sessions.erase(session_id);
    }
    mu.unlock();
    return clask::response {
      .code = 302,
      .content = "",
      .headers = {
        {"Location", "/"},
      }
    };
  });
  s.GET("/", [&](clask::request& req) -> clask::response {
    auto session_id = req.cookie_value("session-id");
    nlohmann::json ctx;
    if (!session_id.empty()) {
      mu.lock();
      if (sessions.count(session_id) > 0) {
        user u = sessions[session_id];
        ctx["user"]["id"] = u.id;
        ctx["user"]["username"] = u.username;
        ctx["user"]["realname"] = u.realname;
      } else if (req.uri_params["err"] == "auth") {
        ctx["err"] = "auth";
      }
      mu.unlock();
    }
    return clask::response {
      .code = 200,
      .content = env.render(temp, ctx),
    };
  });
  s.run();
}
