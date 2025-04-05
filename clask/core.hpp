#ifndef INCLUDE_CLASK_HPP_
#define INCLUDE_CLASK_HPP_

#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

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
#include <iomanip>
#include <thread>
#include <filesystem>
#include <chrono>
#include <codecvt>

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
  std::cerr << s << ": " << buf << std::endl;
}
typedef char sockopt_t;
#else
# include <unistd.h>
# include <sys/fcntl.h>
# include <sys/types.h>
# include <sys/socket.h>
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

typedef enum class _log_level {ERR, WARN, INFO, DEBUG} log_level;

class logger {
protected:
  std::ostringstream os;
  log_level lv;

private:
  //logger(const logger&);
  logger& operator =(const logger&);

public:
  static log_level default_level;
  logger() : lv(clask::log_level::INFO) {};
  virtual ~logger();
  std::ostringstream& get(log_level level = log_level::INFO);
  static log_level& level();
};

inline logger& logger::operator =(const logger& l) {
  this->lv = l.lv;
  return *this;
}

inline std::ostringstream& logger::get(log_level level) {
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
  lv = level;
  return os;
}

inline logger::~logger() {
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

inline std::wstring to_wstring(const std::string& input) {
  try {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(input);
  } catch (std::range_error&) {
    size_t length = input.length();
    std::wstring result;
    result.reserve(length);
    for (size_t i = 0; i < length; i++) {
      result.push_back(input[i] & 0xFF);
    }
    return result;
  }
}

inline std::string camelize(std::string& s) {
  auto n = s.length();
  for (size_t i = 0; i < n; i++) {
    if (i == 0 || s[i - 1] == ' ' || s[i - 1] == '-') {
      s[i] = (char) std::toupper(s[i]);
      continue;
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
    if (sub.substr(0, 5) == "name=") {
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
    if (sub.substr(0, 9) == "filename=") {
      sub = sub.substr(9);
      trim_string(sub, "\"");
      return sub;
    }
    if (sub.substr(0, 10) == "filename*=") {
      sub = sub.substr(10);
      for (auto& c : sub) c = (char) std::tolower(c);
      if (sub.substr(0, 7) == "utf-8''") {
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
  write(std::string(buf, n));
}

inline void response_writer::write_headers() {
  header_out = true;
  std::ostringstream os;
  os << "HTTP/1.1 " << code << " " << status_codes[code] << "\r\n";
  auto res_headers = os.str();
  send(s, res_headers.data(), (int) res_headers.size(), MSG_NOSIGNAL);
  for (auto& h : headers) {
    auto hh = h.first + ": " + h.second + "\r\n";
    send(s, hh.data(), (int) hh.size(), MSG_NOSIGNAL);
  }
  send(s, "\r\n", 2, MSG_NOSIGNAL);
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
    if (sub.substr(0, 9) == "boundary=") {
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
    std::ostringstream os;
    os << "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\n";
    os << "Connection: " << (keep_alive ? "Keep-Alive" : "Close") << "\r\n";
    os << "Content-Length: " << res.size() << "\r\n\r\n";
    auto res_headers = os.str();
    send(s, res_headers.data(), (int) res_headers.size(), MSG_NOSIGNAL);
    send(s, res.data(), (int) res.size(), MSG_NOSIGNAL);
  } else if (f_writer != nullptr) {
    response_writer writer(s, 200);
    f_writer(writer, req);
    keep_alive = false;
    code = writer.code;
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
    send(s, res_headers.data(), (int) res_headers.size(), MSG_NOSIGNAL);
    code = res.code;
  }
  return code;
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
  void parse_tree(node&, const std::string&, const func_t&);
  bool match(const std::string&, const std::string&, const std::function<void(const func_t& fn, const std::vector<std::string>&)>&) const;
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
  void run(const std::string&);
  void run(int);
  logger log;
  server_t() : treeGET{}, treePOST{} {}
#ifdef CLASK_TEST
  bool test_match(const std::string&, const std::string&, const std::function<void(const func_t& fn, const std::vector<std::string>&)>&) const;
#endif
};

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
      .name = std::move(sub),
      .placeholder = placeholder,
    };
    parse_tree(nn, s.substr(pos), fn);
    n.children.emplace_back(std::move(nn));
  }
}

inline bool server_t::match(const std::string& method, const std::string& s, const std::function<void(const func_t& fn, const std::vector<std::string>&)>& fn) const {
  node n = method == "GET" ? treeGET : treePOST;
  std::vector<std::string> args;
  auto ss = s;
  std::string sub;
  while (true) {
    auto pos = ss.find('/', 1);

    if (pos == std::string::npos) {
      sub = ss.substr(1);
      pos = ss.size();
    } else {
      sub = ss.substr(1, pos - 1);
    }
    bool found = false;
    for (const auto& vv : n.children) {
      if (vv.placeholder) {
        args.emplace_back(url_decode(sub));
        n = node(vv);
        found = true;
        break;
      } else if (vv.name.empty() || vv.name == sub) {
        n = node(vv);
        found = true;
        break;
      }
    }
    if (!found)
      break;
    ss = ss.substr(pos);
    if (ss.empty() || n.children.empty()) {
      fn(n.fn, args);
      return true;
    }
  }
  return false;
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

#define CLASK_DEFINE_REQUEST(name) \
inline void server_t::GET(const std::string& path, functor_ ## name fn) { \
  parse_tree(treeGET, path, func_t { .f_ ## name = std::move(fn) }); \
} \
inline void server_t::POST(const std::string& path, functor_ ## name fn) { \
  parse_tree(treePOST, path, func_t { .f_ ## name = std::move(fn) }); \
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
      (void) std::get_time(&file_gmt, h.second.c_str());
      if (std::mktime(&file_gmt) <= std::mktime(gmt)) {
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
  parse_tree(treeGET, path, func_t {
    .f_writer = [path, dir, listing](response_writer& resp, request& req) {
      std::vector<std::string> paths;
      std::string p = req.uri;
      while (true) {
        auto pos = p.find('/', 1);
        if (pos == std::string::npos) {
          auto sub = p.substr(1);
          if (sub == "..") paths.pop_back();
          else paths.emplace_back(sub);
          break;
        } else {
          auto sub = p.substr(1, pos - 1);
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
      auto res = std::mismatch(req_path.begin(), req_path.end(), path.begin());
      if (res.first == path.end()) {
        resp.code = 404;
        resp.set_header("content-type", "text/plain");
        resp.write("Not Found");
        return;
      }
      req_path = url_decode(req_path.substr(path.size()));
      req_path = dir + "/" + req_path;
      if (req_path[req_path.size() - 1] == '/') {
        if (listing) {
          serve_dir(resp, req, req_path);
          return;
        }
        req_path += "index.html";
      }

      serve_file(resp, req, req_path);
    }
  });
}

inline void server_t::_run(const std::string& host, int port = 8080) {
  int server_fd, s;
  struct sockaddr_in address{};
  sockopt_t opt = 1;
  int addrlen = sizeof(address);

#ifdef _WIN32
  WSADATA wsa;
  (void) WSAStartup(MAKEWORD(2, 0), &wsa);
#endif

  sort_handlers(treeGET);
  sort_handlers(treePOST);

#if 0
  for (auto v : treeGET.children) {
    std::cout << v.name << std::endl;
    std::cout << v.placeholder << std::endl;
  }
#endif

  if ((server_fd = (int) socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    throw std::runtime_error("socket failed");
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, (int) sizeof(opt))) {
    throw std::runtime_error("setsockopt");
  }
  address.sin_family = AF_INET;
  if (host.empty())
    address.sin_addr.s_addr = htonl(INADDR_ANY);
  else {
    auto info = gethostbyname(host.c_str());
    if (info == nullptr) {
      throw std::runtime_error("gethostbyname");
    }
    address.sin_addr = *(struct in_addr *)(info->h_addr_list[0]);
  }
  address.sin_port = htons((u_short) port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    throw std::runtime_error("bind failed");
  }
  if (listen(server_fd, SOMAXCONN) < 0) {
    throw std::runtime_error("listen");
  }

  while (true) {
    if ((s = (int) accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
      throw std::runtime_error("accept");
    }

    std::thread t([&](int s, const std::string& remote) {
retry:
      char buf[4096];
      const char *method, *path;
      int pret, minor_version;
      struct phr_header headers[100];
      size_t buflen = 0, prevbuflen = 0, method_len, path_len, num_headers;
      ssize_t rret;

      while (true) {
        while ((rret = recv(s, buf + buflen, (int) (sizeof(buf) - buflen), MSG_NOSIGNAL)) == -1 && errno == EINTR);
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
#ifndef CLASK_DISABLE_LOGS
          logger().get(log_level::ERR) << "invalid request";
#endif
          return;
        }
        if (buflen == sizeof(buf)) {
          // RequestIsTooLongError
#ifndef CLASK_DISABLE_LOGS
          logger().get(log_level::ERR) << "request is too long";
#endif
          continue;
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
            // IOError
            closesocket(s);
            return;
          }
          req_body.append(buf, rret);
          rest -= rret;
        }
      }

      request req(
          req_method,
          req_raw_path,
          req_path,
          std::move(req_uri_params),
          std::move(req_headers),
          std::move(req_body));

      if (!match(req_method, req_path, [&](const func_t& fn, const std::vector<std::string>& args) {
        req.args = args;
        int code = 500;
        try {
          code = fn.handle(s, req, keep_alive);
#ifndef CLASK_DISABLE_LOGS
          logger().get(log_level::INFO) << remote << " " << code << " " << req_method << " " << req_path;
#endif
        } catch (std::exception&) {
#ifndef CLASK_DISABLE_LOGS
          logger().get(log_level::WARN) << remote << " " << code << " " << req_method << " " << req_path;
#endif
          std::ostringstream os;
          os << "HTTP/1.0 " << code << " Internal Server Error\r\nContent-Type: text/plain\r\n\r\nInternal Server Error";
          send(s, os.str().data(), (int) os.str().size(), MSG_NOSIGNAL);
        }
      })) {
#ifndef CLASK_DISABLE_LOGS
        logger().get(log_level::WARN) << remote << " " << 404 << " " << req_method << " " << req_path;
#endif
        static const std::string res_content = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
        send(s, res_content.data(), (int) res_content.size(), MSG_NOSIGNAL);
      }

      if (keep_alive)
        goto retry;
      closesocket(s);
    }, s, (std::string) inet_ntoa(address.sin_addr));
    t.detach();
  }
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
