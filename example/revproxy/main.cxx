#include <clask/core.hpp>
#include <nlohmann/json.hpp>

// Usage: example-revproxy [revproxy.json]
//
// {
//   "listen": "0.0.0.0:8080",        // optional, defaults to port 8080
//   "worker_count": 32,              // optional
//   "timeout": 30000,                // optional, default upstream timeout (ms)
//   "proxies": [
//     { "path": "/api/", "upstream": "http://127.0.0.1:8081/v1/", "timeout": 5000 }
//   ]
// }
int main(int argc, char* argv[]) {
  std::string config_path = argc > 1 ? argv[1] : "revproxy.json";
  std::ifstream ifs(config_path);
  if (!ifs) {
    std::cerr << "cannot open " << config_path << "\n";
    return 1;
  }

  auto s = clask::server();
  s.log.default_level = clask::log_level::INFO;
  std::string listen;
  try {
    auto config = nlohmann::json::parse(ifs);
    listen = config.value("listen", "");
    if (config.contains("worker_count")) {
      s.worker_count(config["worker_count"].get<unsigned int>());
    }
    auto timeout = config.value("timeout", 30000);
    auto& proxies = config.at("proxies");
    if (!proxies.is_array() || proxies.empty()) {
      throw std::invalid_argument("\"proxies\" must be a non-empty array");
    }
    for (const auto& p : proxies) {
      auto path = p.at("path").get<std::string>();
      auto upstream = p.at("upstream").get<std::string>();
      s.reverse_proxy(path, upstream, p.value("timeout", timeout));
      std::cerr << path << " -> " << upstream << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << config_path << ": " << e.what() << "\n";
    return 1;
  }

  if (listen.empty()) {
    s.run();
  } else {
    s.run(listen);
  }
}
