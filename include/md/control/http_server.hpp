#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace md {

struct HttpRequest {
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers;  // lower-case names
  std::string body;
};

struct HttpResponse {
  int status{200};
  std::string content_type{"application/json"};
  std::string body;
};

// Parses a complete HTTP/1.1 request from `raw`. Returns nullopt if `raw` is
// not yet complete (need more bytes); throws std::invalid_argument if it is
// malformed. Pure function so it can be unit-tested without sockets.
[[nodiscard]] std::optional<HttpRequest> parse_http_request(std::string_view raw,
                                                            std::size_t max_body = 64 * 1024);

[[nodiscard]] std::string format_http_response(const HttpResponse& r);

// Minimal blocking HTTP/1.1 server for the operator control panel.
// One background thread, one connection at a time, `Connection: close`.
// That is plenty for a single operator and keeps the code small enough to
// audit; it is NOT meant to face the internet (binds to 127.0.0.1 by default).
class HttpServer {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&)>;

  HttpServer(std::string bind_address, int port, Handler handler);
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  // Port actually bound (useful when constructed with port 0).
  [[nodiscard]] int port() const { return port_; }
  void stop();

 private:
  void run();
  void serve_one(int fd);

  Handler handler_;
  int listen_fd_{-1};
  int port_{0};
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace md
