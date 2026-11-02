#include "md/control/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace md {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.remove_suffix(1);
  return s;
}

const char* reason(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 413:
      return "Payload Too Large";
    default:
      return "Error";
  }
}

}  // namespace

std::optional<HttpRequest> parse_http_request(std::string_view raw, std::size_t max_body) {
  const std::size_t header_end = raw.find("\r\n\r\n");
  if (header_end == std::string_view::npos) {
    if (raw.size() > 16 * 1024) {
      throw std::invalid_argument("headers too large");
    }
    return std::nullopt;
  }
  HttpRequest req;
  std::string_view head = raw.substr(0, header_end);

  const std::size_t line_end = head.find("\r\n");
  const std::string_view request_line = head.substr(0, line_end);
  const std::size_t sp1 = request_line.find(' ');
  const std::size_t sp2 = request_line.find(' ', sp1 == std::string_view::npos ? 0 : sp1 + 1);
  if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
    throw std::invalid_argument("bad request line");
  }
  req.method = std::string(request_line.substr(0, sp1));
  req.path = std::string(request_line.substr(sp1 + 1, sp2 - sp1 - 1));
  if (const std::size_t q = req.path.find('?'); q != std::string::npos) {
    req.path.resize(q);  // query strings are not used by this API
  }

  std::string_view rest =
      line_end == std::string_view::npos ? std::string_view{} : head.substr(line_end + 2);
  while (!rest.empty()) {
    const std::size_t e = rest.find("\r\n");
    const std::string_view line = rest.substr(0, e);
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      throw std::invalid_argument("bad header line");
    }
    req.headers[lower(std::string(trim(line.substr(0, colon))))] =
        std::string(trim(line.substr(colon + 1)));
    if (e == std::string_view::npos) break;
    rest.remove_prefix(e + 2);
  }

  std::size_t content_length = 0;
  if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
    try {
      content_length = std::stoul(it->second);
    } catch (const std::exception&) {
      throw std::invalid_argument("bad content-length");
    }
  }
  if (content_length > max_body) {
    throw std::length_error("body too large");
  }
  const std::size_t body_start = header_end + 4;
  if (raw.size() < body_start + content_length) {
    return std::nullopt;
  }
  req.body = std::string(raw.substr(body_start, content_length));
  return req;
}

std::string format_http_response(const HttpResponse& r) {
  std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
  out += "Content-Type: " + r.content_type + "\r\n";
  out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
  out += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
  out += r.body;
  return out;
}

HttpServer::HttpServer(std::string bind_address, int port, Handler handler)
    : handler_(std::move(handler)) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0) {
    throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
  }
  const int yes = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
    ::close(listen_fd_);
    throw std::invalid_argument("bad bind address: " + bind_address);
  }
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      ::listen(listen_fd_, 8) != 0) {
    const std::string err = std::strerror(errno);
    ::close(listen_fd_);
    throw std::runtime_error("bind/listen on " + bind_address + ":" + std::to_string(port) + ": " +
                             err);
  }
  socklen_t len = sizeof(addr);
  ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
  port_ = ntohs(addr.sin_port);
  thread_ = std::thread([this] { run(); });
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::stop() {
  stop_.store(true, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
}

void HttpServer::run() {
  while (!stop_.load(std::memory_order_acquire)) {
    // Poll with a timeout so stop() is noticed within 100 ms.
    pollfd p{listen_fd_, POLLIN, 0};
    if (::poll(&p, 1, 100) <= 0) {
      continue;
    }
    const int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    serve_one(fd);
    ::close(fd);
  }
}

void HttpServer::serve_one(int fd) {
  std::string raw;
  char buf[4096];
  HttpResponse resp;
  try {
    for (;;) {
      pollfd p{fd, POLLIN, 0};
      if (::poll(&p, 1, 2000) <= 0) {
        return;  // slow or idle client: drop it rather than block the operator
      }
      const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) {
        return;
      }
      raw.append(buf, static_cast<std::size_t>(n));
      if (auto req = parse_http_request(raw)) {
        resp = handler_(*req);
        break;
      }
    }
  } catch (const std::length_error&) {
    resp = {413, "application/json", R"({"error":"body too large"})"};
  } catch (const std::exception& e) {
    resp = {400, "application/json", std::string(R"({"error":"bad request"})")};
  }
  const std::string out = format_http_response(resp);
  std::size_t sent = 0;
  while (sent < out.size()) {
    const ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      return;
    }
    sent += static_cast<std::size_t>(n);
  }
}

}  // namespace md
