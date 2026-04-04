#ifndef INCLUDE_CLASK_HPP_
#define INCLUDE_CLASK_HPP_

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <exception>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
# include <ws2tcpip.h>
inline static void socket_perror(const char *s) {
  char buf[512];
  FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM,
      nullptr,
      WSAGetLastError(),
      0,
      buf,
      sizeof(buf) / sizeof(buf[0]),
      nullptr);
  std::cerr << s << ": " << buf << "\n";
}
typedef char sockopt_t;
#else
# include <unistd.h>
# include <sys/fcntl.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <poll.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
#define closesocket(fd) close(fd)
#define socket_perror(s) perror(s)
typedef int sockopt_t;
#endif

#include "picohttpparser.h"
#include "picohttpparser.c"

#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#endif

namespace clask {

constexpr int keep_alive_timeout_ms = 5000;
constexpr size_t accept_queue_factor = 64;
constexpr unsigned int default_worker_count = 4;

struct socket_wait_event {
  int fd;
  bool readable;
  bool closed;
};

struct socket_wait_result {
  bool server_readable;
  std::vector<socket_wait_event> events;
};

struct connection_state {
  int fd;
  std::string remote;
};

struct completed_connection {
  connection_state conn;
  bool keep_alive;
};

struct server_runtime_state {
  std::mutex ready_queue_mu;
  std::condition_variable ready_queue_cv;
  std::deque<connection_state> ready_queue;
  std::mutex completed_queue_mu;
  std::deque<completed_connection> completed_queue;
  std::unordered_map<int, connection_state> idle_connections;
  std::atomic<size_t> tracked_connections{0};
};

inline void drain_completed_connections(server_runtime_state& runtime);
inline void accept_ready_connection(
    int server_fd,
    size_t accept_queue_limit,
    server_runtime_state& runtime);
inline void requeue_readable_idle_connections(
    const std::vector<socket_wait_event>& events,
    server_runtime_state& runtime);

inline bool set_socket_timeout(int s, int optname, int timeout_ms) {
#ifdef _WIN32
  DWORD timeout = (DWORD) timeout_ms;
  return setsockopt(s, SOL_SOCKET, optname, (const char*) &timeout, sizeof(timeout)) == 0;
#else
  struct timeval timeout = {
    .tv_sec = timeout_ms / 1000,
    .tv_usec = (timeout_ms % 1000) * 1000,
  };
  return setsockopt(s, SOL_SOCKET, optname, &timeout, sizeof(timeout)) == 0;
#endif
}

inline socket_wait_result wait_socket_events(
    int server_fd,
    const std::unordered_map<int, connection_state>& idle_connections,
    int timeout_ms) {
  socket_wait_result result{
    .server_readable = false,
    .events = {},
  };
#ifdef _WIN32
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET((SOCKET) server_fd, &readfds);
  SOCKET maxfd = (SOCKET) server_fd;
  for (const auto& conn : idle_connections) {
    auto fd = (SOCKET) conn.second.fd;
    FD_SET(fd, &readfds);
    if (fd > maxfd) {
      maxfd = fd;
    }
  }
  timeval timeout = {
    .tv_sec = timeout_ms / 1000,
    .tv_usec = (timeout_ms % 1000) * 1000,
  };
  auto ready = select((int) maxfd + 1, &readfds, nullptr, nullptr, &timeout);
  if (ready < 0) {
    throw std::runtime_error("select");
  }
  result.server_readable = FD_ISSET((SOCKET) server_fd, &readfds);
  result.events.reserve(idle_connections.size());
  for (const auto& conn : idle_connections) {
    if (FD_ISSET((SOCKET) conn.second.fd, &readfds)) {
      result.events.push_back(socket_wait_event {
        .fd = conn.second.fd,
        .readable = true,
        .closed = false,
      });
    }
  }
#else
  std::vector<pollfd> fds;
  fds.reserve(idle_connections.size() + 1);
  fds.push_back(pollfd {
    .fd = server_fd,
    .events = POLLIN,
    .revents = 0,
  });
  for (const auto& conn : idle_connections) {
    fds.push_back(pollfd {
      .fd = conn.second.fd,
      .events = POLLIN,
      .revents = 0,
    });
  }
  auto ready = poll(fds.data(), fds.size(), timeout_ms);
  if (ready < 0) {
    if (errno == EINTR) {
      return result;
    }
    throw std::runtime_error("poll");
  }
  result.server_readable = ready > 0 && (fds.front().revents & POLLIN);
  result.events.reserve(idle_connections.size());
  for (size_t i = 1; i < fds.size(); i++) {
    if (fds[i].revents == 0) {
      continue;
    }
    result.events.push_back(socket_wait_event {
      .fd = fds[i].fd,
      .readable = (fds[i].revents & POLLIN) != 0,
      .closed = (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0,
    });
  }
#endif
  return result;
}

inline void send_service_unavailable_response(int s) {
  static const std::string busy_response =
      "HTTP/1.1 503 Service Unavailable\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: Close\r\n"
      "Content-Length: 19\r\n\r\n"
      "Service Unavailable";
  send(s, busy_response.data(), (int) busy_response.size(), MSG_NOSIGNAL);
}

inline bool accept_connection(
    int server_fd,
    connection_state& conn) {
  struct sockaddr_in client_address{};
  socklen_t client_addrlen = sizeof(client_address);
  auto s = (int) accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen);
  if (s < 0) {
    return false;
  }
  char addr_buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &client_address.sin_addr, addr_buf, INET_ADDRSTRLEN);
  conn = connection_state {
    .fd = s,
    .remote = std::string(addr_buf),
  };
  return true;
}

inline int create_listening_socket(const std::string& host, int port) {
  int server_fd;
  struct sockaddr_in address{};
  sockopt_t opt = 1;

  if ((server_fd = (int) socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    throw std::runtime_error("socket failed");
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, (int) sizeof(opt))) {
    throw std::runtime_error("setsockopt");
  }
  address.sin_family = AF_INET;
  if (host.empty()) {
    address.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
      if (result) freeaddrinfo(result);
      throw std::runtime_error("getaddrinfo failed");
    }
    address.sin_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr;
    freeaddrinfo(result);
  }
  address.sin_port = htons((u_short) port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    throw std::runtime_error("bind failed");
  }
  if (listen(server_fd, SOMAXCONN) < 0) {
    throw std::runtime_error("listen");
  }
  return server_fd;
}

inline void initialize_network_runtime() {
#ifdef _WIN32
  WSADATA wsa;
  (void) WSAStartup(MAKEWORD(2, 0), &wsa);
#endif
}

inline unsigned int resolve_worker_count(unsigned int configured_worker_count) {
  auto worker_count = configured_worker_count;
  if (worker_count == 0) {
    worker_count = std::thread::hardware_concurrency();
    if (worker_count == 0) {
      worker_count = default_worker_count;
    } else {
      worker_count *= 2;
    }
  }
  return worker_count;
}

inline size_t resolve_accept_queue_limit(
    size_t configured_accept_queue_limit,
    unsigned int worker_count) {
  if (configured_accept_queue_limit > 0) {
    return configured_accept_queue_limit;
  }
  return worker_count * accept_queue_factor;
}

template <typename HandleConnectionFn>
inline void start_worker_pool(
    unsigned int worker_count,
    server_runtime_state& runtime,
    HandleConnectionFn&& handle_connection) {
  for (unsigned int n = 0; n < worker_count; n++) {
    std::thread([&]() {
      while (true) {
        connection_state conn;
        {
          std::unique_lock<std::mutex> lk(runtime.ready_queue_mu);
          runtime.ready_queue_cv.wait(lk, [&]() { return !runtime.ready_queue.empty(); });
          conn = std::move(runtime.ready_queue.front());
          runtime.ready_queue.pop_front();
        }
        auto keep_alive = handle_connection(conn.fd, conn.remote);
        {
          std::lock_guard<std::mutex> lk(runtime.completed_queue_mu);
          runtime.completed_queue.push_back(completed_connection{
            .conn = std::move(conn),
            .keep_alive = keep_alive,
          });
        }
      }
    }).detach();
  }
}

template <typename HandleConnectionFn>
inline void run_server_event_loop(
    int server_fd,
    unsigned int worker_count,
    size_t accept_queue_limit,
    server_runtime_state& runtime,
    HandleConnectionFn&& handle_connection) {
  start_worker_pool(
      worker_count,
      runtime,
      std::forward<HandleConnectionFn>(handle_connection));

  while (true) {
    drain_completed_connections(runtime);
    auto wait_result = wait_socket_events(server_fd, runtime.idle_connections, 100);
    if (!wait_result.server_readable && wait_result.events.empty()) {
      continue;
    }

    if (wait_result.server_readable) {
      accept_ready_connection(server_fd, accept_queue_limit, runtime);
    }

    requeue_readable_idle_connections(wait_result.events, runtime);
  }
}

inline void enqueue_ready_connection(
    server_runtime_state& runtime,
    connection_state conn) {
  {
    std::lock_guard<std::mutex> lk(runtime.ready_queue_mu);
    runtime.ready_queue.emplace_back(std::move(conn));
  }
  runtime.ready_queue_cv.notify_one();
}

inline void drain_completed_connections(
    server_runtime_state& runtime) {
  std::deque<completed_connection> drained;
  {
    std::lock_guard<std::mutex> lk(runtime.completed_queue_mu);
    drained.swap(runtime.completed_queue);
  }
  for (auto& conn : drained) {
    if (conn.keep_alive) {
      runtime.idle_connections.emplace(conn.conn.fd, std::move(conn.conn));
    } else {
      runtime.tracked_connections--;
    }
  }
}

inline void accept_ready_connection(
    int server_fd,
    size_t accept_queue_limit,
    server_runtime_state& runtime) {
  if (runtime.tracked_connections.load() >= accept_queue_limit) {
    connection_state conn{};
    if (accept_connection(server_fd, conn)) {
      send_service_unavailable_response(conn.fd);
      closesocket(conn.fd);
    }
    return;
  }

  connection_state conn{};
  if (!accept_connection(server_fd, conn)) {
    throw std::runtime_error("accept");
  }
  runtime.tracked_connections++;
  enqueue_ready_connection(runtime, std::move(conn));
}

inline void requeue_readable_idle_connections(
    const std::vector<socket_wait_event>& events,
    server_runtime_state& runtime) {
  for (const auto& event : events) {
    auto it = runtime.idle_connections.find(event.fd);
    if (it == runtime.idle_connections.end()) {
      continue;
    }
    if (event.closed) {
      closesocket(it->first);
      runtime.tracked_connections--;
      runtime.idle_connections.erase(it);
      continue;
    }
    if (event.readable) {
      enqueue_ready_connection(runtime, std::move(it->second));
      runtime.idle_connections.erase(it);
    }
  }
}

inline void send_text_response(
    int s,
    int code,
    const std::string& reason,
    const std::string& body,
    bool keep_alive) {
  std::ostringstream os;
  os << "HTTP/1.1 " << code << " " << reason
     << "\r\nContent-Type: text/plain\r\nConnection: "
     << (keep_alive ? "Keep-Alive" : "Close")
     << "\r\nContent-Length: " << body.size()
     << "\r\n\r\n" << body;
  send(s, os.str().data(), (int) os.str().size(), MSG_NOSIGNAL);
}


typedef enum class _log_level {ERR, WARN, INFO, DEBUG} log_level;

class logger {
protected:
  std::ostringstream os;
  log_level lv;
  bool enabled;

private:
  logger(const logger&) = delete;
  logger& operator =(const logger&) = delete;

public:
  static log_level default_level;
  logger() : lv(clask::log_level::INFO), enabled(false) {};
  logger(logger&&) = default;
  logger& operator =(logger&&) = default;
  virtual ~logger();
  std::ostringstream& get(log_level level = log_level::INFO);
  static log_level& level();
};

inline std::ostringstream& logger::get(log_level level) {
  lv = level;
  enabled = (lv >= logger::level());
  if (enabled) {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    os << std::put_time(&tm, "%Y/%m/%d %H:%M:%S ");
    switch (level) {
      case log_level::ERR: os << "ERR: "; break;
      case log_level::WARN: os << "WARN: "; break;
      case log_level::INFO: os << "INFO: "; break;
      case log_level::DEBUG: os << "DEBUG: "; break;
      default: break;
    }
  }
  return os;
}

inline logger::~logger() {
  if (enabled) {
    os << "\n";
    std::cerr << os.str();
  }
}

#define CLASK_LOG(lvl) \
  if (lvl < clask::logger::level()) ; \
  else clask::logger().get(lvl)

template <typename TP>
std::time_t to_time_t(TP tp) {
  using namespace std::chrono;
  auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now() + system_clock::now());
  return system_clock::to_time_t(sctp);
}

inline std::wstring to_wstring(const std::string& input) {
  std::wstring result;
  result.reserve(input.length());
  size_t i = 0;
  while (i < input.length()) {
    unsigned char c = input[i];
    if (c < 0x80) {
      result.push_back(c);
      i++;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < input.length()) {
      wchar_t wc = ((c & 0x1F) << 6) | (input[i + 1] & 0x3F);
      result.push_back(wc);
      i += 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < input.length()) {
      wchar_t wc = ((c & 0x0F) << 12) | ((input[i + 1] & 0x3F) << 6) | (input[i + 2] & 0x3F);
      result.push_back(wc);
      i += 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < input.length()) {
      i += 4;
    } else {
      result.push_back(c);
      i++;
    }
  }
  return result;
}

inline std::string camelize(std::string& s) {
  auto n = s.length();
  for (size_t i = 0; i < n; i++) {
    if (i == 0 || s[i - 1] == ' ' || s[i - 1] == '-') {
      s[i] = (char) std::toupper(s[i]);
    } else {
      s[i] = (char) std::tolower(s[i]);
    }
  }
  return s.substr(0, n);
}

inline void trim_string(std::string& s, const std::string& cutsel = " \t\v\r\n") {
  auto left = s.find_first_not_of(cutsel);
  if (left != std::string::npos) {
    auto right = s.find_last_not_of(cutsel);
    s = s.substr(left, right - left + 1);
  }
}

inline std::vector<std::string> split_string(const std::string& s, char delim, size_t max_elems = -1) {
  std::vector<std::string> elems;
  std::stringstream ss(s);
  std::string item;
  while (getline(ss, item, delim)) {
    if (!item.empty() && elems.size() < max_elems) {
      elems.push_back(item);
    }
  }
  return elems;
}

inline std::string html_encode(const std::string& value) {
  std::string buf;
  buf.reserve(value.size());
  for (const char & c : value) {
    switch (c) {
      case '&':  buf.append("&amp;");  break;
      case '\"': buf.append("&quot;"); break;
      case '\'': buf.append("&apos;"); break;
      case '<':  buf.append("&lt;");   break;
      case '>':  buf.append("&gt;");   break;
      default:   buf.append(&c, 1); break;
    }
  }
  return buf;
}

inline std::string url_encode(const std::string &value, bool escape_slash = true) {
  std::ostringstream os;
  os.fill('0');
  os << std::hex;
  for (char c : value) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      os << c;
      continue;
    }
    if (!escape_slash && c == '/') {
      os << c;
      continue;
    }
    os << std::uppercase;
    os << '%' << std::setw(2) << int((unsigned char) c);
    os << std::nouppercase;
  }
  return os.str();
}

inline std::string url_decode(const std::string &s) {
  std::string ret;
  const char* p = s.c_str();
  while (*p) {
    if (*p == '%' && p[1] && p[2] && std::isxdigit(p[1]) && std::isxdigit(p[2])) {
      const int hi = p[1] - (p[1] <= '9' ? '0' : (p[1] <= 'F' ? 'A' : 'a') - 10);
      const int lo = p[2] - (p[2] <= '9' ? '0' : (p[2] <= 'F' ? 'A' : 'a') - 10);
      ret += static_cast<char>(16 * hi + lo);
      p += 3;
      continue;
    }
    ret += *p++;
  }
  return ret;
}

typedef std::pair<std::string, std::string> header;

typedef struct _part {
  std::vector<header> headers;
  std::string body;
  std::string header_value(const std::string&);
  std::string filename();
  std::string name();
} part;

inline std::string part::name() {
  auto cd = header_value("content-disposition");
  while (!cd.empty()) {
    auto pos = cd.find(';');
    if (pos == std::string::npos) {
      pos = cd.size() - 1;
    }
    auto sub = cd.substr(0, pos);
    trim_string(sub, " \t");
    if (sub.size() >= 5 && sub.substr(0, 5) == "name=") {
      sub = sub.substr(5);
      trim_string(sub, "\"");
      return sub;
    }
    cd = cd.substr(pos + 1);
  }
  return "";
}

inline std::string part::filename() {
  auto cd = header_value("content-disposition");
  while (!cd.empty()) {
    auto pos = cd.find(';');
    if (pos == std::string::npos) {
      pos = cd.size() - 1;
    }
    auto sub = cd.substr(0, pos);
    trim_string(sub, " \t");
    if (sub.size() >= 9 && sub.substr(0, 9) == "filename=") {
      sub = sub.substr(9);
      trim_string(sub, "\"");
      return sub;
    }
    if (sub.size() >= 10 && sub.substr(0, 10) == "filename*=") {
      sub = sub.substr(10);
      for (auto& c : sub) c = (char) std::tolower(c);
      if (sub.size() >= 7 && sub.substr(0, 7) == "utf-8''") {
        sub = url_decode(sub.substr(7));
        trim_string(sub, "\"");
        return sub;
      }
      return sub;
    }
    cd = cd.substr(pos + 1);
  }
  return "";
}

inline std::string part::header_value(const std::string& name) {
  std::string key = name;
  camelize(key);
  for (auto& h : headers) {
    if (h.first == key) return h.second;
  }
  return "";
}

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
  { 418, "I'm a teapot" },
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

static std::unordered_map<std::string, std::string> content_types = {
  { ".txt",  "text/plain; charset=utf-8" },
  { ".html", "text/html; charset=utf-8" },
  { ".js",   "text/javascript" },
  { ".json", "text/json" },
  { ".png",  "image/png" },
  { ".jpg",  "image/jpeg" },
  { ".jpeg", "image/jpeg" },
  { ".gif",  "image/gif" },
  { ".css",  "text/css" },
};


class response_writer {
private:
  std::vector<header> headers;
  int s;
  bool header_out;
public:
  response_writer(int s, int code) : s(s), header_out(false), code(code) { }
  int code;
  void set_header(std::string, const std::string&);
  void clear_header();
  virtual void write(const std::string&);
  virtual void write(char*, size_t);
  virtual void write_headers();
  virtual void end();
};

class server_sent_event_writer {
private:
  response_writer w;
public:
  server_sent_event_writer(response_writer& w) : w(w) {
    w.set_header("Transfer-Encoding", "chunked");
  };
  void write(const std::string& event, const std::string& data) {
    w.write("event: " + event + "\r\n");
    w.write("data: " + data + "\r\n");
    w.write("\r\n");
  }
  void end() {
    w.end();
  }
};
class chunked_writer {
private:
  response_writer w;
public:
  chunked_writer(response_writer& w) : w(w) {
    w.set_header("Transfer-Encoding", "chunked");
  };
  void write(const std::string& s) {
    std::stringstream shex;
    shex << std::hex << s.size();
    w.write(shex.str() + "\r\n");
    w.write(s + "\r\n");
  }
  void write(char* ptr, size_t len) {
    std::stringstream shex;
    shex << std::hex << len;
    w.write(shex.str() + "\r\n");
    w.write(ptr, len);
    w.write("\r\n");
  }
  void end() {
    w.write("0\r\n\r\n");
    w.end();
  }
};

inline std::unordered_map<std::string, std::string> params(const std::string& s) {
  std::unordered_map<std::string, std::string> ret;
  std::istringstream iss(s);
  std::string keyval, key, val;
  while(std::getline(iss, keyval, '&')) {
    std::istringstream isk(keyval);
    if(std::getline(std::getline(isk, key, '='), val)) {
      ret[url_decode(key)] = url_decode(val);
    }
  }
  return ret;
}

inline void response_writer::clear_header() {
  headers.clear();
}

inline void response_writer::set_header(std::string key, const std::string& val) {
  auto h = camelize(key);
  for (auto& hh : headers) {
    if (hh.first == h) {
      hh.second = val;
      return;
    }
  }
  headers.emplace_back(h, val);
}

inline void response_writer::write(char* buf, size_t n) {
  if (!header_out) {
    write_headers();
  }
  send(s, buf, (int) n, MSG_NOSIGNAL);
}

inline void response_writer::write_headers() {
  header_out = true;
  std::string buf;
  buf.reserve(256);
  buf += "HTTP/1.1 ";
  buf += std::to_string(code);
  buf += " ";
  buf += status_codes[code];
  buf += "\r\n";
  for (auto& h : headers) {
    buf += h.first;
    buf += ": ";
    buf += h.second;
    buf += "\r\n";
  }
  buf += "\r\n";
  send(s, buf.data(), (int) buf.size(), MSG_NOSIGNAL);
}

inline void response_writer::write(const std::string& content) {
  if (!header_out) {
    write_headers();
  }
  send(s, content.data(), (int) content.size(), MSG_NOSIGNAL);
}

inline void response_writer::end() {
  if (!header_out) {
    write_headers();
  }
  closesocket(s);
}

struct response {
  int code;
  std::string content;
  std::vector<header> headers;
};

struct request {
  std::string method;
  std::string raw_uri;
  std::string uri;
  std::unordered_map<std::string, std::string> uri_params;
  std::vector<header> headers;
  std::string body;
  std::vector<std::string> args;

  request(
      std::string method, std::string raw_uri, std::string uri,
      std::unordered_map<std::string, std::string> uri_params,
      std::vector<header> headers, std::string body)
    : method(std::move(method)), raw_uri(std::move(raw_uri)),
      uri(std::move(uri)), uri_params(std::move(uri_params)),
      headers(std::move(headers)), body(std::move(body)) { }

  bool parse_multipart(std::vector<part>& parts);
  std::string header_value(const std::string&);
  std::string cookie_value(const std::string&);
};

inline bool request::parse_multipart(std::vector<part>& parts) {
  parts.clear();

  auto ct = header_value("content-type");
  std::string boundary;
  while (!ct.empty()) {
    auto pos = ct.find(';');
    if (pos == std::string::npos) {
      pos = ct.size();
    }
    auto sub = ct.substr(0, pos);
    trim_string(sub, " \t");
    if (sub.size() >= 9 && sub.substr(0, 9) == "boundary=") {
      sub = sub.substr(9);
      trim_string(sub, "\"");
      boundary = sub;
      break;
    }
    ct = ct.substr(pos + 1);
  }
  if (boundary.empty()) {
    return false;
  }
  boundary = "--" + boundary;

  size_t pos = 0;
  while (true) {
    auto next = body.find(boundary + "\r\n", pos + 1);
    if (next == std::string::npos) {
      next = body.find(boundary + "--\r\n", pos + 1);
      if (next == std::string::npos) {
        break;
      }
      boundary += "--";
    }

    auto data = body.substr(pos + boundary.size(), next);

    auto eos = data.find("\r\n\r\n");
    if (eos == std::string::npos) {
      return false;
    }
    auto lines = data.substr(0, eos + 4);

    struct phr_header hdrs[100];
    size_t prevbuflen = 0, num_headers;

    num_headers = sizeof(hdrs) / sizeof(hdrs[0]);
    prevbuflen = 0;
    auto pret = phr_parse_headers(
        lines.data(), lines.size(), hdrs, &num_headers, prevbuflen);
    if (pret <= 0) {
      return false;
    }

    std::vector<header> req_headers;

    for (size_t n = 0; n < num_headers; n++) {
      auto key = std::string(hdrs[n].name, hdrs[n].name_len);
      auto val = std::string(hdrs[n].value, hdrs[n].value_len);
      key = url_decode(key);
      val = url_decode(val);
      req_headers.emplace_back(std::move(key), std::move(val));
    }

    part p = {
      .headers = std::move(req_headers),
      .body = data = data.substr(eos + 4)
    };
    parts.emplace_back(std::move(p));
    pos = next + boundary.size() + 1;
    if ((body.at(pos) == '-' && body.at(pos + 1) == '-'
        && body.at(pos + 2) == '\n') || body.at(pos) != '\n') {
      break;
    }
  }
  return true;
}

inline std::string request::header_value(const std::string& name) {
  std::string key = name;
  camelize(key);
  for (auto&& h : headers) {
    if (h.first == key) return h.second;
  }
  return "";
}

inline std::string request::cookie_value(const std::string& name) {
  std::string value, path;
  for (auto&& header : headers) {
    if (header.first == "Cookie") {
      auto elems = split_string(header.second, ';');
      auto found = false;
      for (auto&& elem : elems) {
        auto v = elem;
        trim_string(v);
        auto toks = split_string(v, '=');
        if (toks.size() == 2 && toks[0] == name) {
          value = toks[1];
          found = true;
        }
        if (toks.size() == 2 && toks[0] == "path") {
          path = toks[1];
        }
      }
      if (found) {
        if (path.empty() || uri.substr(path.size()) == path) {
          return value;
        }
      }
    }
  }
  return "";
}

struct request_read_result {
  bool ok;
  bool keep_alive;
  int error_code;
  const char* error_reason;
  const char* error_body;
  std::optional<request> req;
};

inline request_read_result read_request_from_socket(int s) {
  char buf[16384];
  const char *method, *path;
  int pret, minor_version;
  struct phr_header headers[100];
  size_t buflen = 0, prevbuflen = 0, method_len, path_len, num_headers;
  ssize_t rret;

  while (true) {
    while ((rret = recv(s, buf + buflen, (int) (sizeof(buf) - buflen), MSG_NOSIGNAL)) == -1 && errno == EINTR);
    if (rret <= 0) {
      return request_read_result{
        .ok = false,
        .keep_alive = false,
        .error_code = 0,
        .error_reason = "",
        .error_body = "",
        .req = std::nullopt,
      };
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
      return request_read_result{
        .ok = false,
        .keep_alive = false,
        .error_code = 400,
        .error_reason = "Bad Request",
        .error_body = "Invalid Request",
        .req = std::nullopt,
      };
    }
    if (buflen == sizeof(buf)) {
      return request_read_result{
        .ok = false,
        .keep_alive = false,
        .error_code = 413,
        .error_reason = "Payload Too Large",
        .error_body = "Request Too Large",
        .req = std::nullopt,
      };
    }
  }

  const std::string req_method(method, method_len);
  std::string req_path(path, path_len);
  std::string req_body(buf + pret, buflen - pret);
  const std::string req_raw_path = req_path;
  std::unordered_map<std::string, std::string> req_uri_params;
  std::vector<header> req_headers;

  auto pos = req_path.find('?');
  if (pos != std::string::npos) {
    req_path.resize(pos);
    std::istringstream iss(req_raw_path.substr(pos + 1));
    std::string keyval, key, val;
    while (std::getline(iss, keyval, '&')) {
      std::istringstream isk(keyval);
      if(std::getline(std::getline(isk, key, '='), val)) {
        req_uri_params[url_decode(key)] = url_decode(val);
      }
    }
  }

  bool keep_alive = minor_version == 1;
  bool has_content_length = false;
  size_t content_length = 0;
  for (size_t n = 0; n < num_headers; n++) {
    auto key = std::string(headers[n].name, headers[n].name_len);
    auto val = std::string(headers[n].value, headers[n].value_len);
    camelize(key);
    if (key == "Content-Length") {
      content_length = std::stoi(val);
      has_content_length = true;
    } else if (key == "Connection") {
      for (auto& c : val) c = (char) std::tolower(c);
      if (val == "keep-alive")
        keep_alive = true;
      else if (val == "close")
        keep_alive = false;
    }
    req_headers.emplace_back(std::move(key), std::move(val));
  }

  if (has_content_length && buflen - pret < content_length) {
    auto rest = content_length - (buflen - pret);
    buflen = 0;
    while (rest > 0) {
      while ((rret = recv(s, buf + buflen, (int) (sizeof(buf) - buflen), MSG_NOSIGNAL)) == -1 && errno == EINTR);
      if (rret <= 0) {
        return request_read_result{
          .ok = false,
          .keep_alive = false,
          .error_code = 0,
          .error_reason = "",
          .error_body = "",
          .req = std::nullopt,
        };
      }
      req_body.append(buf, rret);
      rest -= rret;
    }
  }

  return request_read_result{
    .ok = true,
    .keep_alive = keep_alive,
    .error_code = 0,
    .error_reason = "",
    .error_body = "",
    .req = request(
        req_method,
        req_raw_path,
        req_path,
        std::move(req_uri_params),
        std::move(req_headers),
        std::move(req_body)),
  };
}

typedef std::function<void(response_writer&, request&)> functor_writer;
typedef std::function<std::string(request&)> functor_string;
typedef std::function<response(request&)> functor_response;

typedef struct _func_t {
  functor_writer f_writer;
  functor_string f_string;
  functor_response f_response;
  int handle(int, request&, bool&) const;
} func_t;

inline int func_t::handle(int s, request& req, bool& keep_alive) const {
  int code = 200;
  if (f_string != nullptr) {
    auto res = f_string(req);
    std::string hdr;
    hdr.reserve(128 + res.size());
    hdr += "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\nConnection: ";
    hdr += keep_alive ? "Keep-Alive" : "Close";
    hdr += "\r\nContent-Length: ";
    hdr += std::to_string(res.size());
    hdr += "\r\n\r\n";
    hdr += res;
    send(s, hdr.data(), (int) hdr.size(), MSG_NOSIGNAL);
  } else if (f_writer != nullptr) {
    response_writer writer(s, 200);
    writer.set_header("Connection", "Close");
    f_writer(writer, req);
    keep_alive = false;
    code = writer.code;
  } else if (f_response != nullptr) {
    auto res = f_response(req);
    auto has_connection = false;
    std::string hdr;
    hdr.reserve(256 + res.content.size());
    hdr += "HTTP/1.1 ";
    hdr += std::to_string(res.code);
    hdr += " ";
    hdr += status_codes[res.code];
    hdr += "\r\n";
    for (auto& h : res.headers) {
      auto key = camelize(h.first);
      if (key == "Content-Length")
        continue;
      if (key == "Connection")
        has_connection = true;
      hdr += key;
      hdr += ": ";
      hdr += h.second;
      hdr += "\r\n";
    }
    if (!has_connection) {
      hdr += "Connection: ";
      hdr += keep_alive ? "Keep-Alive" : "Close";
      hdr += "\r\n";
    }
    hdr += "Content-Length: ";
    hdr += std::to_string(res.content.size());
    hdr += "\r\n\r\n";
    hdr += res.content;
    send(s, hdr.data(), (int) hdr.size(), MSG_NOSIGNAL);
    code = res.code;
  }
  return code;
}

template <typename MatchFn>
inline bool dispatch_request(
    MatchFn&& match_fn,
    int s,
    const std::string& remote,
    request& req,
    bool& keep_alive) {
  if (!match_fn(req.method, req.uri, [&](const func_t& fn, const std::vector<std::string>& args) {
    req.args = args;
    int code = 500;
    try {
      code = fn.handle(s, req, keep_alive);
#ifndef CLASK_DISABLE_LOGS
      CLASK_LOG(clask::log_level::INFO) << remote << " " << code << " " << req.method << " " << req.uri;
#endif
    } catch (std::exception&) {
#ifndef CLASK_DISABLE_LOGS
      CLASK_LOG(clask::log_level::WARN) << remote << " " << code << " " << req.method << " " << req.uri;
#endif
      keep_alive = false;
      send_text_response(s, code, "Internal Server Error", "Internal Server Error", keep_alive);
    }
  })) {
#ifndef CLASK_DISABLE_LOGS
    CLASK_LOG(clask::log_level::WARN) << remote << " " << 404 << " " << req.method << " " << req.uri;
#endif
    send_text_response(s, 404, "Not Found", "Not Found", keep_alive);
  }
  return keep_alive;
}

template <typename MatchFn>
inline bool handle_connection_request(
    int s,
    const std::string& remote,
    int socket_timeout_ms,
    MatchFn&& match_fn) {
  if (!set_socket_timeout(s, SO_RCVTIMEO, socket_timeout_ms)
      || !set_socket_timeout(s, SO_SNDTIMEO, socket_timeout_ms)) {
    closesocket(s);
    return false;
  }

  auto read_result = read_request_from_socket(s);
  if (!read_result.ok) {
#ifndef CLASK_DISABLE_LOGS
    if (read_result.error_code == 400) {
      CLASK_LOG(clask::log_level::ERR) << "invalid request";
    } else if (read_result.error_code == 413) {
      CLASK_LOG(clask::log_level::ERR) << "request is too long";
    }
#endif
    if (read_result.error_code != 0) {
      send_text_response(
          s,
          read_result.error_code,
          read_result.error_reason,
          read_result.error_body,
          false);
    }
    closesocket(s);
    return false;
  }

  auto req = std::move(*read_result.req);
  auto keep_alive = read_result.keep_alive;
  dispatch_request(
      std::forward<MatchFn>(match_fn),
      s,
      remote,
      req,
      keep_alive);
  if (!keep_alive) {
    closesocket(s);
  }
  return keep_alive;
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
  unsigned int worker_count_;
  size_t accept_queue_limit_;
  int socket_timeout_ms_;
  void parse_tree(node&, const std::string&, const func_t&);
  bool match(const std::string&, const std::string&, const std::function<void(const func_t& fn, const std::vector<std::string>&)>&) const;
  bool handle_connection_socket(int, const std::string&) const;
  void _run(const std::string&, int);

public:
#define CLASK_DEFINE_REQUEST(name) \
void GET(const std::string&, const functor_ ## name); \
void POST(const std::string&, const functor_ ## name);
  CLASK_DEFINE_REQUEST(writer)
  CLASK_DEFINE_REQUEST(string)
  CLASK_DEFINE_REQUEST(response)
#undef CLASK_DEFINE_REQUEST
  void static_dir(const std::string&, const std::string&, bool listing = false);
  server_t& worker_count(unsigned int) &;
  server_t&& worker_count(unsigned int) &&;
  server_t& accept_queue_limit(size_t) &;
  server_t&& accept_queue_limit(size_t) &&;
  server_t& socket_timeout(int) &;
  server_t&& socket_timeout(int) &&;
  void run(const std::string&);
  void run(int);
  logger log;
  server_t() : treeGET{}, treePOST{}, worker_count_{0}, accept_queue_limit_{0}, socket_timeout_ms_{keep_alive_timeout_ms} {}
#ifdef CLASK_TEST
  bool test_match(const std::string&, const std::string&, const std::function<void(const func_t& fn, const std::vector<std::string>&)>&) const;
#endif
};

inline server_t& server_t::worker_count(unsigned int v) & {
  worker_count_ = v;
  return *this;
}

inline server_t&& server_t::worker_count(unsigned int v) && {
  worker_count_ = v;
  return std::move(*this);
}

inline server_t& server_t::accept_queue_limit(size_t v) & {
  accept_queue_limit_ = v;
  return *this;
}

inline server_t&& server_t::accept_queue_limit(size_t v) && {
  accept_queue_limit_ = v;
  return std::move(*this);
}

inline server_t& server_t::socket_timeout(int v) & {
  socket_timeout_ms_ = v;
  return *this;
}

inline server_t&& server_t::socket_timeout(int v) && {
  socket_timeout_ms_ = v;
  return std::move(*this);
}

inline void server_t::parse_tree(node& n, const std::string& s, const func_t& fn) {
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
      node nn =  {
        .children = {},
        .name = std::move(sub),
        .fn = fn,
        .placeholder = placeholder,
      };
      n.children.emplace_back(nn);
    }
    return;
  }
  auto sub = s.substr(1, pos - 1);
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
      .children = {},
      .name = std::move(sub),
      .fn = {},
      .placeholder = placeholder,
    };
    parse_tree(nn, s.substr(pos), fn);
    n.children.emplace_back(std::move(nn));
  }
}

inline bool server_t::match(const std::string& method, const std::string& s, const std::function<void(const func_t& fn, const std::vector<std::string>&)>& fn) const {
  const node* n = method == "GET" ? &treeGET : &treePOST;
  std::vector<std::string> args;
  size_t offset = 0;
  while (offset < s.size()) {
    auto pos = s.find('/', offset + 1);

    std::string sub;
    if (pos == std::string::npos) {
      sub = s.substr(offset + 1);
      pos = s.size();
    } else {
      sub = s.substr(offset + 1, pos - offset - 1);
    }
    bool found = false;
    for (const auto& vv : n->children) {
      if (vv.placeholder) {
        args.emplace_back(url_decode(sub));
        n = &vv;
        found = true;
        break;
      } else if (vv.name.empty() || vv.name == sub) {
        n = &vv;
        found = true;
        break;
      }
    }
    if (!found)
      break;
    offset = pos;
    if (offset >= s.size() || n->children.empty()) {
      fn(n->fn, args);
      return true;
    }
  }
  return false;
}

inline bool server_t::handle_connection_socket(int s, const std::string& remote) const {
  return handle_connection_request(
      s,
      remote,
      socket_timeout_ms_,
      [&](const std::string& method, const std::string& path, const auto& fn) {
        return match(method, path, fn);
      });
}

#ifdef CLASK_TEST
bool server_t::test_match(const std::string& method, const std::string& s, const std::function<void(const func_t& fn, const std::vector<std::string>&)>& fn) const {
  return server_t::match(method, s, fn);
}
#endif

inline void sort_handlers(node& n) {
  std::sort(
    n.children.begin(),
    n.children.end(),
    [](const node& x, const node& y){ return x.name > y.name; }
  );
  for (auto& v : n.children) {
    sort_handlers(v);
  }
}

inline void prepare_handler_trees(node& treeGET, node& treePOST) {
  sort_handlers(treeGET);
  sort_handlers(treePOST);

#if 0
  for (auto v : treeGET.children) {
    std::cout << v.name << std::endl;
    std::cout << v.placeholder << std::endl;
  }
#endif
}

#define CLASK_DEFINE_REQUEST(name) \
inline void server_t::GET(const std::string& path, functor_ ## name fn) { \
  func_t func{}; \
  func.f_ ## name = std::move(fn); \
  parse_tree(treeGET, path, func); \
} \
inline void server_t::POST(const std::string& path, functor_ ## name fn) { \
  func_t func{}; \
  func.f_ ## name = std::move(fn); \
  parse_tree(treePOST, path, func); \
}

CLASK_DEFINE_REQUEST(writer)
CLASK_DEFINE_REQUEST(string)
CLASK_DEFINE_REQUEST(response)

#undef CLASK_DEFINE_REQUEST

inline void serve_dir(response_writer& resp, request& req, const std::string& path) {
  auto wpath = to_wstring(path);

  resp.code = 200;
  resp.set_header("content-type", "text/html; charset=utf-8");
  std::ostringstream os;
  os << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>";
  os << html_encode(req.uri);
  os << "</title>\n</head>\n<body>\n";
  resp.write(os.str());
  os.str("");
  os.clear(std::stringstream::goodbit);

  for (const auto& e : std::filesystem::directory_iterator(wpath)) {
    auto fn = e.path().filename().string();
    if (e.is_directory()) fn += "/";
    os << "<a href=\"" << url_encode(fn, false) << "\">" << html_encode(fn) << "</a></br>\n";
    resp.write(os.str());
    os.str("");
    os.clear(std::stringstream::goodbit);
  }
  resp.write("</body>\n</html>\n");
}

inline void serve_file(response_writer& resp, request& req, const std::string& path) {
  auto wpath = to_wstring(path);
  std::filesystem::path fspath(wpath.c_str());

  std::ifstream is(fspath, std::ios::in | std::ios::binary);
  if (is.fail()) {
    resp.clear_header();
    resp.code = 404;
    resp.set_header("content-type", "text/plain");
    resp.write("Not Found");
    return;
  }

  auto it = content_types.find(fspath.extension().string());
  if (it != content_types.end()) {
    resp.set_header("content-type", it->second);
  }

  std::uintmax_t size = std::filesystem::file_size(fspath);
  resp.set_header("content-length", std::to_string(size));

  std::filesystem::file_time_type file_time = std::filesystem::last_write_time(fspath);
  std::time_t tt = to_time_t(file_time);
  std::tm *gmt = std::gmtime(&tt);
  for (auto& h : req.headers) {
    if (h.first == "If-Modified-Since") {
      std::tm file_gmt{};
      std::istringstream ss(h.second);
      ss >> std::get_time(&file_gmt, "%a, %d %B %Y %H:%M:%S");
      if (!ss.fail() && std::mktime(&file_gmt) <= std::mktime(gmt)) {
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
    resp.write(buf, (size_t) is.read(buf, sizeof(buf)).gcount());
  }
}

inline void server_t::static_dir(const std::string& path, const std::string& dir, bool listing) {
  func_t func{};
  func.f_writer = [path, dir, listing](response_writer& resp, request& req) {
    auto req_path = req.uri;
    if (req_path.find("..") != std::string::npos) {
      resp.code = 403;
      resp.set_header("content-type", "text/plain");
      resp.write("Forbidden");
      return;
    }

    auto res = std::mismatch(req_path.begin(), req_path.end(), path.begin());
    if (res.first != req_path.begin() + path.size()) {
      resp.code = 404;
      resp.set_header("content-type", "text/plain");
      resp.write("Not Found");
      return;
    }

    req_path = url_decode(req_path.substr(path.size()));
    if (req_path.find("..") != std::string::npos) {
      resp.code = 403;
      resp.set_header("content-type", "text/plain");
      resp.write("Forbidden");
      return;
    }

    req_path = dir + "/" + req_path;
    if (!req_path.empty() && req_path[req_path.size() - 1] == '/') {
      if (listing) {
        serve_dir(resp, req, req_path);
        return;
      }
      req_path += "index.html";
    }

    serve_file(resp, req, req_path);
  };
  parse_tree(treeGET, path, func);
}

inline void server_t::_run(const std::string& host, int port = 8080) {
  initialize_network_runtime();
  prepare_handler_trees(treeGET, treePOST);

  auto server_fd = create_listening_socket(host, port);

  server_runtime_state runtime;
  const auto worker_count = resolve_worker_count(worker_count_);
  const auto accept_queue_limit = resolve_accept_queue_limit(accept_queue_limit_, worker_count);

  run_server_event_loop(
      server_fd,
      worker_count,
      accept_queue_limit,
      runtime,
      [&](int s, const std::string& remote) {
        return handle_connection_socket(s, remote);
      });
}

inline void server_t::run(const std::string& addr) {
  auto pos = addr.find_last_of(':');
  if (pos == std::string::npos) {
    throw std::runtime_error("invalid host:port");
  }
  auto host = addr.substr(0, pos);
  auto port = std::stoi(addr.substr(pos + 1));
  _run(host, port);
}

inline void server_t::run(int port = 8080) {
  _run("", port);
}

inline server_t server() { return server_t{}; }

log_level logger::default_level = log_level::INFO;

inline log_level& logger::level() {
  return default_level;
}

}

#endif  // INCLUDE_CLASK_HPP_
