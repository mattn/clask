#ifndef INCLUDE_CLASK_HPP_
#define INCLUDE_CLASK_HPP_

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>
#include <string>
#include <unordered_map>
#include <exception>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <iomanip>
#include <thread>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
# include <ws2tcpip.h>
static void
socket_perror(const char *s) {
  char buf[512];
  FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM,
      nullptr,
      WSAGetLastError(),
      0,
      buf,
      sizeof(buf) / sizeof(buf[0]),
      nullptr);
  std::cerr << s << ": " << buf << std::endl;
}
typedef char sockopt_t;
#else
# include <unistd.h>
# include <sys/fcntl.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netdb.h>
#define closesocket(fd) close(fd)
#define socket_perror(s) perror(s)
typedef int sockopt_t;
#endif

#include "picohttpparser.h"
#include "picohttpparser.c"

namespace clask {

enum log_level {ERR, WARN, INFO, DEBUG};

class logger {
protected:
  std::ostringstream os;
  log_level lv;

private:
  logger(const logger&);
  logger& operator =(const logger&);

public:
  static log_level default_level;
  logger() {};
  virtual ~logger();
  std::ostringstream& get(log_level level = INFO);
  static log_level& level();
};

std::ostringstream& logger::get(log_level level) {
  auto t = std::time(nullptr);
  auto tm = *std::localtime(&t);
  os << std::put_time(&tm, "%Y/%m/%d %H:%M:%S ");
  switch (level) {
    case ERR: os << "ERR: "; break;
    case WARN: os << "WARN: "; break;
    case INFO: os << "INFO: "; break;
    default: os << "DEBUG: "; break;
  }
  lv = level;
  return os;
}

logger::~logger() {
  if (lv >= logger::level()) {
    os << std::endl;
    std::cerr << os.str().c_str();
  }
}

template <typename TP>
std::time_t to_time_t(TP tp) {
  using namespace std::chrono;
  auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now() + system_clock::now());
  return system_clock::to_time_t(sctp);
}

typedef std::pair<std::string, std::string> header;

static std::unordered_map<int, std::string> status_codes = {
  { 100, "Continue" },
  { 101, "Switching Protocols" },
  { 102, "Processing" },
  { 200, "OK" },
  { 201, "Created" },
  { 202, "Accepted" },
  { 203, "Non-Authoritative Information" },
  { 204, "No Content" },
  { 205, "Reset Content" },
  { 206, "Partial Content" },
  { 207, "Multi-Status" },
  { 208, "Already Reported" },
  { 300, "Multiple Choices" },
  { 301, "Moved Permanently" },
  { 302, "Found" },
  { 303, "See Other" },
  { 304, "Not Modified" },
  { 305, "Use Proxy" },
  { 307, "Temporary Redirect" },
  { 400, "Bad Request" },
  { 401, "Unauthorized" },
  { 402, "Payment Required" },
  { 403, "Forbidden" },
  { 404, "Not Found" },
  { 405, "Method Not Allowed" },
  { 406, "Not Acceptable" },
  { 407, "Proxy Authentication Required" },
  { 408, "Request Timeout" },
  { 409, "Conflict" },
  { 410, "Gone" },
  { 411, "Length Required" },
  { 412, "Precondition Failed" },
  { 413, "Request Entity Too Large" },
  { 414, "Request-URI Too Large" },
  { 415, "Unsupported Media Type" },
  { 416, "Request Range Not Satisfiable" },
  { 417, "Expectation Failed" },
  { 418, "I""m a teapot" },
  { 422, "Unprocessable Entity" },
  { 423, "Locked" },
  { 424, "Failed Dependency" },
  { 425, "No code" },
  { 426, "Upgrade Required" },
  { 428, "Precondition Required" },
  { 429, "Too Many Requests" },
  { 431, "Request Header Fields Too Large" },
  { 449, "Retry with" },
  { 500, "Internal Server Error" },
  { 501, "Not Implemented" },
  { 502, "Bad Gateway" },
  { 503, "Service Unavailable" },
  { 504, "Gateway Timeout" },
  { 505, "HTTP Version Not Supported" },
  { 506, "Variant Also Negotiates" },
  { 507, "Insufficient Storage" },
  { 509, "Bandwidth Limit Exceeded" },
  { 510, "Not Extended" },
  { 511, "Network Authentication Required" },
};

class response_writer {
private:
  std::vector<header> headers;
  bool header_out;
  int s;
public:
  int code;
  response_writer(int s, int code) : s(s), code(code), header_out(false) { }
  void set_header(std::string, std::string);
  void clear_header();
  void write(const std::string&);
  void write(char*, size_t);
  void end();
  friend std::istream & operator >> (std::istream&, response_writer&);
};

static std::string url_decode(const std::string &s) {
  std::string ret;
  int v;
  for (auto i = 0; i < s.length(); i++) {
    if (s[i] == '%') {
      std::sscanf(s.substr(i+1,2).c_str(), "%x", &v);
      ret += static_cast<char>(v);
      i += 2;
    } else {
      ret += s[i];
    }
  }
  return std::move(ret);
}

std::unordered_map<std::string, std::string> params(std::string& s) {
  std::unordered_map<std::string, std::string> ret;
  std::istringstream iss(s);
  std::string keyval, key, val;
  while(std::getline(iss, keyval, '&')) {
    std::istringstream isk(keyval);
    if(std::getline(std::getline(isk, key, '='), val)) {
      ret[std::move(url_decode(key))] = std::move(url_decode(val));
    }
  }
  return std::move(ret);
}

std::string camelize(std::string& s) {
  int n = s.length();
  for (auto i = 0; i < n; i++) {
    if (i == 0 || s[i-1] == ' ' || s[i-1] == '-') {
      s[i] = std::toupper(s[i]);
      continue;
    }
  }
  return std::move(s.substr(0, n));
}

void response_writer::clear_header() {
  headers.clear();
}

void response_writer::set_header(std::string key, std::string val) {
  auto h = camelize(key);
  for (auto& hh : headers) {
    if (hh.first == h) {
      hh.second = val;
      return;
    }
  }
  headers.emplace_back(std::move(std::make_pair(h, val)));
}

void response_writer::write(char* buf, size_t n) {
  write(std::string(buf, n));
}

void response_writer::write(const std::string& content) {
  if (!header_out) {
    header_out = true;
    std::ostringstream os;
    os << "HTTP/1.0 " << code << " " << status_codes[code] << "\r\n";
    auto res_headers = os.str();
    send(s, res_headers.data(), res_headers.size(), 0);
    for (auto& h : headers) {
      auto hh = h.first + ": " + h.second + "\r\n";
      send(s, hh.data(), hh.size(), 0);
    }
    send(s, "\r\n", 2, 0);
  }
  send(s, content.data(), content.size(), 0);
}

void response_writer::end() {
  closesocket(s);
}

struct response {
  int code;
  std::string content;
  std::vector<header> headers;
};

struct request {
  std::string method;
  std::string uri;
  std::string raw_uri;
  std::vector<header> headers;
  std::unordered_map<std::string, std::string> uri_params;
  std::string body;
  std::vector<std::string> args;

  request(
      std::string method, std::string raw_uri, std::string uri,
      std::unordered_map<std::string, std::string> uri_params,
      std::vector<header> headers, std::string body)
    : method(method), raw_uri(std::move(raw_uri)),
      uri(std::move(uri)), uri_params(std::move(uri_params)),
      headers(std::move(headers)), body(std::move(body)) { }
};

typedef std::function<void(response_writer&, request&)> functor_writer;
typedef std::function<std::string(request&)> functor_string;
typedef std::function<response(request&)> functor_response;

typedef struct {
  functor_writer f_writer;
  functor_string f_string;
  functor_response f_response;
  void handle(int, request&, bool&) const;
} func_t;

void func_t::handle(int s, request& req, bool& keep_alive) const {
  if (f_string != nullptr) {
    auto res = f_string(req);
    std::ostringstream os;
    os << "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\n";
    os << "Connection: " << (keep_alive ? "Keep-Alive" : "Close") << "\r\n";
    os << "Content-Length: " << res.size() << "\r\n\r\n";
    auto res_headers = os.str();
    send(s, res_headers.data(), res_headers.size(), 0);
    send(s, res.data(), res.size(), 0);
  } else if (f_writer != nullptr) {
    response_writer writer(s, 200);
    f_writer(writer, req);
    keep_alive = false;
  } else if (f_response != nullptr) {
    auto res = f_response(req);
    std::ostringstream os;
    os << "HTTP/1.1 " << res.code << " " << status_codes[res.code] << "\r\n";
    for (auto& h : res.headers) {
      auto key = camelize(h.first);
      if (key == "Content-Length")
        continue;
      os << key + ": " + h.second + "\r\n";
    }
    os << "Content-Length: " << res.content.size() << "\r\n\r\n";
    auto res_headers = os.str();
    send(s, res_headers.data(), res_headers.size(), 0);
    send(s, res.content.data(), res.content.size(), 0);
  }
}

typedef struct _node {
  std::vector<struct _node> children;
  std::string name;
  func_t fn;
  bool placeholder;
} node;

class server_t {
private:
  std::string compiled_tree;
  node treeGET;
  node treePOST;
  void parse_tree(node&, const std::string&, const func_t);
  bool match(const std::string&, const std::string&, std::function<void(const func_t& fn, const std::vector<std::string>&)>) const;
  std::pair<std::string, std::string> static_dir_pair;

public:
#define CLASK_DEFINE_REQUEST(name) \
void GET(const std::string&, const functor_ ## name); \
void POST(const std::string&, const functor_ ## name);
  CLASK_DEFINE_REQUEST(writer)
  CLASK_DEFINE_REQUEST(string)
  CLASK_DEFINE_REQUEST(response)
#undef CLASK_DEFINE_REQUEST
  void static_dir(const std::string&, const std::string&);
  void run(int);
  logger log;
};

void server_t::parse_tree(node& n, const std::string& s, const func_t fn) {
  auto pos = s.find('/', 1);
  auto placeholder = s[1] == ':';
  if (pos == std::string::npos) {
    bool found = false;
    auto sub = s.substr(placeholder ? 2 : 1);
    for (auto& vv : n.children) {
      if (vv.name == sub) {
        found = true;
        break;
      }
    }
    if (!found) {
      n.children.emplace_back(std::move(node {
        .name = std::move(sub),
        .fn = fn,
        .placeholder = placeholder,
      }));
    }
    return;
  }
  auto sub = s.substr(1, pos-1);
  if (placeholder) sub = sub.substr(1);
  bool found = false;
  for (auto& vv : n.children) {
    if (vv.name == sub) {
      found = true;
      parse_tree(vv, s.substr(pos), fn);
      break;
    }
  }
  if (!found) {
    node nn = {
      .name = std::move(sub),
      .placeholder = placeholder,
    };
    parse_tree(nn, s.substr(pos), fn);
    n.children.emplace_back(std::move(nn));
  }
}

bool server_t::match(const std::string& method, const std::string& s, std::function<void(const func_t& fn, const std::vector<std::string>&)> fn) const {
  node n = method == "GET" ? treeGET : treePOST;
  std::vector<std::string> args;
  auto ss = s;
  std::string sub;
  while (1) {
    auto pos = ss.find('/', 1);

    if (pos == std::string::npos) {
      sub = ss.substr(1);
      pos = ss.size();
    } else {
      sub = ss.substr(1, pos-1);
    }
    bool found = false;
    for (const auto vv : n.children) {
      if (vv.placeholder) {
        args.emplace_back(std::move(url_decode(sub)));
        n = vv;
        found = true;
        break;
      } else if (vv.name.empty() || vv.name == sub) {
        n = vv;
        found = true;
        break;
      }
    }
    if (!found)
      break;
    ss = ss.substr(pos);
    if (ss.empty() || n.children.size() == 0) {
      fn(n.fn, std::move(args));
      return true;
    }
  }
  return false;
}

void sort_handlers(node& n) {
  std::sort(
    n.children.begin(),
    n.children.end(),
    [](const node& x, const node& y){ return x.name > y.name; }
  );
  for (auto& v : n.children) {
    sort_handlers(v);
  }
}

static std::string methodGET = "GET ";
static std::string methodPOST = "POST ";

#define CLASK_DEFINE_REQUEST(name) \
void server_t::GET(const std::string& path, functor_ ## name fn) { \
  parse_tree(treeGET, path, func_t { .f_ ## name = fn }); \
} \
void server_t::POST(const std::string& path, functor_ ## name fn) { \
  parse_tree(treePOST, path, func_t { .f_ ## name = fn }); \
}

CLASK_DEFINE_REQUEST(writer);
CLASK_DEFINE_REQUEST(string);
CLASK_DEFINE_REQUEST(response);

#undef CLASK_DEFINE_REQUEST

void server_t::static_dir(const std::string& path, const std::string& dir) {
  static std::string path_ = path, dir_ = dir;

  parse_tree(treeGET, path, func_t {
    .f_writer = [&](response_writer& resp, request& req) {
      std::vector<std::string> paths;
      std::string p = req.uri;
      while (1) {
        auto pos = p.find('/', 1);
        if (pos == std::string::npos) {
          auto sub = p.substr(1);
          if (sub == "..") paths.pop_back();
          else paths.emplace_back(sub);
          break;
        } else {
          auto sub = p.substr(1, pos-1);
          if (sub == "..") paths.pop_back();
          else paths.emplace_back(sub);
        }
        p = p.substr(pos);
      }
      std::ostringstream os;
      for (auto it = paths.begin(); it != paths.end(); ++it) {
        if (it != paths.end()) os << "/";
        os << *it;
      }
      auto req_path = os.str();
      auto res = std::mismatch(req_path.begin(), req_path.end(), path_.begin());
      if (res.first == path_.end()) {
        resp.code = 404;
        resp.set_header("content-type", "text/plain");
        resp.write("Not Found");
        return;
      }
      req_path = dir_ + "/" + req_path.substr(path_.size());
      if (req_path[req_path.size()-1] == '/') req_path += "/index.html";

      std::ifstream is(req_path, std::ios::in | std::ios::binary);
      if (is.fail()) {
        resp.clear_header();
        resp.code = 404;
        resp.set_header("content-type", "text/plain");
        resp.write("Not Found");
        return;
      }

      std::filesystem::path fspath(req_path);
      std::uintmax_t size = std::filesystem::file_size(fspath);
      resp.set_header("content-length", std::to_string(size));

      std::filesystem::file_time_type file_time = std::filesystem::last_write_time(req_path);
      std::time_t tt = to_time_t(file_time);
      std::tm *gmt = std::gmtime(&tt);
      for (auto& h : req.headers) {
        if (h.first == "If-Modified-Since") {
          std::tm fgmt;
          std::get_time(&fgmt, h.second.c_str());
          if (std::mktime(&fgmt) <= std::mktime(gmt)) {
            resp.clear_header();
            resp.code = 304;
            resp.set_header("content-type", "text/plain");
            resp.write("Not Modified");
            return;
          }
          break;
        }
      }

      std::stringstream date;
      date << std::put_time(gmt, "%a, %d %B %Y %H:%M:%S GMT");
      resp.set_header("last-modified", date.str());

      char buf[BUFSIZ];
      while (!is.eof()) {
        auto size = is.read(buf, sizeof(buf)).gcount();
        resp.write(buf, size);
      }
    }
  });
}

void server_t::run(int port = 8080) {
  int server_fd, s;
  struct sockaddr_in address;
  sockopt_t opt = 1;
  int addrlen = sizeof(address);

#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 0), &wsa);
#endif

  sort_handlers(treeGET);
  sort_handlers(treePOST);

#if 0
  for (auto v : treeGET.children) {
    std::cout << v.name << std::endl;
    std::cout << v.placeholder << std::endl;
  }
#endif

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    throw std::runtime_error("socket failed");
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, (int) sizeof(opt))) {
    throw std::runtime_error("setsockopt");
  }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    throw std::runtime_error("bind failed");
  }
  if (listen(server_fd, SOMAXCONN) < 0) {
    throw std::runtime_error("listen");
  }

  while (true) {
    if ((s = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
      throw std::runtime_error("accept");
    }

    std::thread t([&](int s) {
retry:
      char buf[4096];
      const char *method, *path;
      int pret, minor_version;
      struct phr_header headers[100];
      size_t buflen = 0, prevbuflen = 0, method_len, path_len, num_headers;
      ssize_t rret;

      while (1) {
        while ((rret = recv(s, buf + buflen, sizeof(buf) - buflen, 0)) == -1 && errno == EINTR);
        if (rret <= 0) {
          // IOError
          closesocket(s);
          return;
        }

        prevbuflen = buflen;
        buflen += rret;
        num_headers = sizeof(headers) / sizeof(headers[0]);
        pret = phr_parse_request(
            buf, buflen, &method, &method_len, &path, &path_len,
            &minor_version, headers, &num_headers, prevbuflen);
        if (pret > 0) {
          break;
        }
        if (pret == -1) {
          // ParseError
          logger().get(ERR) << "invalid request";
          return;
        }
        if (buflen == sizeof(buf)) {
          // RequestIsTooLongError
          logger().get(ERR) << "request is too long";
          continue;
        }
      }

      const std::string req_method(method, method_len);
      std::string req_path(path, path_len);
      const std::string req_body(buf + pret, buflen - pret);
      const std::string req_raw_path = req_path;
      std::unordered_map<std::string, std::string> req_uri_params;
      std::vector<header> req_headers;

      auto pos = req_path.find('?');
      if (pos != req_path.npos) {
        req_path.resize(pos);
        std::istringstream iss(req_raw_path);
        std::string keyval, key, val;
        while (std::getline(iss, keyval, '&')) {
          std::istringstream isk(keyval);
          if(std::getline(std::getline(isk, key, '='), val)) {
            req_uri_params[std::move(url_decode(key))] = std::move(url_decode(val));
          }
        }
      }

      bool keep_alive = false;
      for (auto n = 0; n < num_headers; n++) {
        auto key = std::string(headers[n].name, headers[n].name_len);
        auto val = std::string(headers[n].value, headers[n].value_len);
        key = std::move(url_decode(key));
        val = std::move(url_decode(val));
        if (key == "Connection") {
          for (auto& c : val) c = std::tolower(c);
          if (val == "keep-alive")
            keep_alive = true;
        }
        req_headers.emplace_back(std::move(std::make_pair(std::move(key), std::move(val))));
      }

      logger().get(INFO) << req_method << " " << req_path;

      request req(
          req_method,
          std::move(req_raw_path),
          req_path,
          std::move(req_uri_params),
          std::move(req_headers),
          std::move(req_body));

      if (!match(req_method, req_path, [&](const func_t& fn, const std::vector<std::string>& args) {
        req.args = std::move(args);
        fn.handle(s, req, keep_alive);
      })) {
        static const std::string res_content = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
        send(s, res_content.data(), res_content.size(), 0);
      }

      if (keep_alive)
        goto retry;
      closesocket(s);
    }, s);
    t.detach();
  }
}

server_t server() { return server_t{}; }

log_level logger::default_level = INFO;

log_level& logger::level() {
  return default_level;
}

}

#endif  // INCLUDE_CLASK_HPP_
