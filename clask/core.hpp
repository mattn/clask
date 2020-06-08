#include <iostream>
#include <functional>
#include <utility>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <exception>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>

#include <ctime>

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
#else
# include <unistd.h>
# include <sys/fcntl.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netdb.h>
#define closesocket(fd) close(fd)
#define socket_perror(s) perror(s)
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
  void write(std::string);
};

static std::string camelize(std::string s) {
  int n = s.length();
  for (auto i = 0; i < n; i++) {
    if (i == 0 || s[i-1] == ' ' || s[i-1] == '-') {
      s[i] = std::toupper(s[i]);
      continue;
    }
  }
  return s.substr(0, n);
}

void response_writer::set_header(std::string key, std::string value) {
  auto h = camelize(key);
  for (auto hh : headers) {
    if (hh.first == h) {
      hh.second = value;
      return;
    }
  }
  headers.push_back(std::make_pair(h, value));
}

void response_writer::write(std::string content) {
  if (!header_out) {
    header_out = true;
    std::ostringstream os;
    os << "HTTP/1.0 " << code << " " << status_codes[code] << "\r\n";
    std::string res_headers = os.str();
    send(s, res_headers.data(), res_headers.size(), 0);
    for (auto h : headers) {
      auto hh = h.first + ": " + h.second + "\r\n";
      send(s, hh.data(), hh.size(), 0);
    }
    send(s, "\r\n", 2, 0);
  }
  send(s, content.data(), content.size(), 0);
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
  functor_writer fw;
  functor_string fs;
  functor_response fr;
} func_t;

class server_t {
private:
  std::unordered_map<std::string, func_t> handlers;

public:
  void GET(std::string path, functor_writer fn);
  void POST(std::string path, functor_writer fn);
  void GET(std::string path, functor_string fn);
  void POST(std::string path, functor_string fn);
  void GET(std::string path, functor_response fn);
  void POST(std::string path, functor_response fn);
  void run();
  logger log;
};

static std::string methodGET = "GET ";
static std::string methodPOST = "POST ";

void server_t::GET(std::string path, functor_writer fn) {
  handlers[methodGET + path].fw = fn;
}

void server_t::POST(std::string path, functor_writer fn) {
  handlers[methodPOST + path].fw = fn;
}

void server_t::GET(std::string path, functor_string fn) {
  handlers[methodGET + path].fs = fn;
}

void server_t::POST(std::string path, functor_string fn) {
  handlers[methodPOST + path].fs = fn;
}

void server_t::GET(std::string path, functor_response fn) {
  handlers[methodGET + path].fr = fn;
}

void server_t::POST(std::string path, functor_response fn) {
  handlers[methodPOST + path].fr = fn;
}

void server_t::run() {
  int server_fd;
  struct sockaddr_in address;
#ifdef _WIN32
  char opt = 1;
#else
  int opt = 1;
#endif
  int addrlen = sizeof(address);

#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 0), &wsa);
#endif

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    throw std::runtime_error("socket failed");
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, (int) sizeof(opt))) {
    throw std::runtime_error("setsockopt");
  }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(8080);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    throw std::runtime_error("bind failed");
  }
  if (listen(server_fd, SOMAXCONN) < 0) {
    throw std::runtime_error("listen");
  }

  while (true) {
    int s;
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
          continue;
        }
        if (buflen == sizeof(buf)) {
          // RequestIsTooLongError
          continue;
        }
      }

      std::string req_method(method, method_len);
      std::string req_path(path, path_len);
      std::string req_body(buf + pret, buflen - pret);

      std::istringstream iss(req_path);
      std::unordered_map<std::string, std::string> req_uri_params;
      std::vector<header> req_headers;

      if (std::getline(iss, req_path, '?')) {
        std::string keyval, key, val;
        while(std::getline(iss, keyval, '&')) {
          std::istringstream isk(keyval);
          // TODO unescape query strings
          if(std::getline(std::getline(isk, key, '='), val))
            req_uri_params[std::move(key)] = std::move(val);
        }
      }
      bool keep_alive = false;
      for (auto n = 0; n < num_headers; n++) {
        auto key = std::string(headers[n].name, headers[n].name_len);
        auto value = std::string(headers[n].value, headers[n].value_len);
        if (key == "Connection" && value == "Keep-Alive")
          keep_alive = true;
        req_headers.push_back(std::move(std::make_pair(std::move(key), std::move(value))));
      }

      logger().get(INFO) << req_method << " " << req_path;

      request req(
          req_method,
          req_path,
          req_path,
          std::move(req_uri_params),
          std::move(req_headers),
          std::move(req_body));

      // TODO
      // bloom filter to handle URL parameters
	  auto it = handlers.find(req_method + " " + req_path);
      if (it != handlers.end()) {
        if (it->second.fs != nullptr) {
          auto res = it->second.fs(req);
          std::ostringstream os;
          os << "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
          os << "Connection: " << (keep_alive ? "Keep-Alive" : "Close") << "\r\n\r\n";
          os << "Content-Length: " << res.size() << "\r\n\r\n";
          auto res_headers = os.str();
          send(s, res_headers.data(), res_headers.size(), 0);
          send(s, res.data(), res.size(), 0);
        } else if (it->second.fw != nullptr) {
          response_writer writer(s, 200);
          it->second.fw(writer, req);
        } else if (it->second.fr != nullptr) {
          auto res = it->second.fr(req);
          std::ostringstream os;
          os << "HTTP/1.1 " << res.code << " " << status_codes[res.code] << "\r\n";
          std::string res_headers = os.str();
          send(s, res_headers.data(), res_headers.size(), 0);
          for (auto h : res.headers) {
            auto hh = h.first + ": " + h.second + "\r\n";
            send(s, hh.data(), hh.size(), 0);
          }
          send(s, "\r\n", 2, 0);
          send(s, res.content.data(), res.content.size(), 0);
        }
      } else {
        std::string res_headers = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
        send(s, res_headers.data(), res_headers.size(), 0);
      }

      if (keep_alive)
        goto retry;
      closesocket(s);
    }, s);
    t.detach();
  }
}

auto server() { return server_t{}; }

log_level logger::default_level = INFO;

log_level& logger::level() {
  return default_level;
}

}
