#include "md/control/command.hpp"
#include "md/control/http_server.hpp"
#include "md/core/channel.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace {

using md::CommandType;

TEST(Command, ParsesGoal) {
  const auto c = md::parse_command(R"({"type":"goal","team":"away","text":"No. 9"})");
  EXPECT_EQ(c.type, CommandType::Goal);
  EXPECT_EQ(c.team, 1);
  EXPECT_EQ(c.text, "No. 9");
}

TEST(Command, RejectsBadInput) {
  EXPECT_THROW((void)md::parse_command("not json"), std::invalid_argument);
  EXPECT_THROW((void)md::parse_command(R"({"type":"explode"})"), std::invalid_argument);
  EXPECT_THROW((void)md::parse_command(R"({"type":"goal","team":"left"})"), std::invalid_argument);
  EXPECT_THROW((void)md::parse_command(R"({"type":"score","home":-1,"away":0})"),
               std::invalid_argument);
  EXPECT_THROW((void)md::parse_command(R"({"type":"replay","seconds":999})"),
               std::invalid_argument);
}

TEST(Command, TruncatesLongText) {
  const auto c = md::parse_command(
      R"({"type":"lower_third","text":"0123456789012345678901234567890123456789012345678901234567890123456789"})");
  EXPECT_EQ(c.text.size(), 60U);
}

TEST(Command, EventsSortedAndRequireTime) {
  const auto ev = md::parse_events(
      R"([{"t":5,"type":"kickoff"},{"t":1,"type":"teams","home":"A","away":"B"}])");
  ASSERT_EQ(ev.size(), 2U);
  EXPECT_EQ(ev[0].type, CommandType::SetTeams);
  EXPECT_THROW((void)md::parse_events(R"([{"type":"kickoff"}])"), std::invalid_argument);
}

TEST(Scoreboard, GoalIncrementsAndShowsCaption) {
  md::ScoreboardState s;
  s.apply(md::parse_command(R"({"type":"goal","team":"home"})"), 10.0, 600.0);
  EXPECT_EQ(s.home_score, 1);
  EXPECT_EQ(s.away_score, 0);
  EXPECT_NE(s.lower_third.find("GOAL"), std::string::npos);
  EXPECT_DOUBLE_EQ(s.lower_third_start, 10.0);
  s.apply(md::parse_command(R"({"type":"kickoff"})"), 11.0, 650.0);
  EXPECT_DOUBLE_EQ(s.kickoff_source_time, 650.0);
}

TEST(Http, ParsesCompleteRequest) {
  const std::string raw =
      "POST /api/command?x=1 HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\n\r\nabcd";
  const auto r = md::parse_http_request(raw);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->method, "POST");
  EXPECT_EQ(r->path, "/api/command");
  EXPECT_EQ(r->headers.at("host"), "a");
  EXPECT_EQ(r->body, "abcd");
}

TEST(Http, IncompleteRequestNeedsMoreBytes) {
  EXPECT_FALSE(md::parse_http_request("GET / HTTP/1.1\r\nHost: a\r\n").has_value());
  EXPECT_FALSE(
      md::parse_http_request("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc").has_value());
}

TEST(Http, RejectsOversizedBody) {
  EXPECT_THROW(
      (void)md::parse_http_request("POST / HTTP/1.1\r\nContent-Length: 999999\r\n\r\n", 1024),
      std::length_error);
}

TEST(Http, ServerRoundTrip) {
  md::HttpServer server("127.0.0.1", 0, [](const md::HttpRequest& req) {
    return md::HttpResponse{200, "text/plain", req.method + " " + req.path};
  });
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<std::uint16_t>(server.port()));
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  const std::string req = "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n";
  ASSERT_EQ(::send(fd, req.data(), req.size(), 0), static_cast<ssize_t>(req.size()));
  std::string resp;
  char buf[512];
  ssize_t n = 0;
  while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) resp.append(buf, static_cast<std::size_t>(n));
  ::close(fd);
  EXPECT_NE(resp.find("200 OK"), std::string::npos);
  EXPECT_NE(resp.find("GET /hello"), std::string::npos);
}

TEST(Channel, BlockingTransferAndClose) {
  auto ch = std::make_unique<md::Channel<int, 4>>();
  std::thread producer([&] {
    for (int i = 0; i < 10000; ++i) {
      int v = i;
      ASSERT_TRUE(ch->push(std::move(v)));
    }
    ch->close();
  });
  int expected = 0;
  int v = 0;
  while (ch->pop(v)) {
    ASSERT_EQ(v, expected++);
  }
  producer.join();
  EXPECT_EQ(expected, 10000) << "close() drains remaining items before pop fails";
  int x = 1;
  EXPECT_FALSE(ch->push(std::move(x)));
}

}  // namespace
