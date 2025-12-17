#include "server/network_server.h"
#include "server/server.h"

#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

using jubilant::server::NetworkServer;
using jubilant::server::Server;

namespace {

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef MSG_WAITALL
#define MSG_WAITALL 0
#endif

bool SendAll(int fd, const std::byte* buffer, std::size_t length) {
  std::size_t offset = 0;
  while (offset < length) {
    const auto sent = ::send(fd, buffer + offset, length - offset, MSG_NOSIGNAL);
    if (sent <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(sent);
  }
  return true;
}

bool WriteFrame(int fd, const nlohmann::json& json) {
  const auto payload = json.dump();
  const auto length = static_cast<std::uint32_t>(payload.size());
  const auto network_length = htonl(length);

  if (!SendAll(fd, reinterpret_cast<const std::byte*>(&network_length), sizeof(network_length))) {
    return false;
  }
  return SendAll(fd, reinterpret_cast<const std::byte*>(payload.data()), payload.size());
}

std::optional<std::string> ReadFrame(int fd) {
  std::array<std::byte, 4> prefix{};
  std::size_t offset = 0;
  while (offset < prefix.size()) {
    const auto bytes = ::recv(fd, prefix.data() + offset, prefix.size() - offset, 0);
    if (bytes <= 0) {
      return std::nullopt;
    }
    offset += static_cast<std::size_t>(bytes);
  }

  std::uint32_t length = 0;
  std::memcpy(&length, prefix.data(), prefix.size());
  length = ntohl(length);
  if (length == 0 || length > (1U << 20)) {
    return std::nullopt;
  }

  std::string payload(length, '\0');
  offset = 0;
  while (offset < payload.size()) {
    const auto bytes = ::recv(fd, payload.data() + offset, payload.size() - offset, MSG_WAITALL);
    if (bytes <= 0) {
      return std::nullopt;
    }
    offset += static_cast<std::size_t>(bytes);
  }

  return payload;
}

std::optional<nlohmann::json> ReadJsonFrame(int fd) {
  const auto payload = ReadFrame(fd);
  if (!payload.has_value()) {
    return std::nullopt;
  }

  const auto json = nlohmann::json::parse(*payload, nullptr, false);
  if (json.is_discarded()) {
    return std::nullopt;
  }
  return json;
}

} // namespace

TEST(NetworkServerTest, ExecutesTransactionsOverTcp) {
  const auto dir = std::filesystem::temp_directory_path() / "jubilant-network-server";
  std::filesystem::remove_all(dir);

  Server core_server{dir, 2};
  core_server.Start();

  NetworkServer::Config config{};
  config.host = "127.0.0.1";
  config.port = 0;
  NetworkServer network{core_server, config};
  ASSERT_TRUE(network.Start());
  ASSERT_GT(network.port(), 0);

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);

  timeval timeout{};
  timeout.tv_sec = 2;
  timeout.tv_usec = 0;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(network.port());
  ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr), 1);
  ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

  nlohmann::json set_request;
  set_request["txn_id"] = 1;
  set_request["operations"] = nlohmann::json::array();
  set_request["operations"].push_back(
      {{"type", "set"}, {"key", "alpha"}, {"value", {{"kind", "string"}, {"data", "bravo"}}}});

  ASSERT_TRUE(WriteFrame(fd, set_request));
  const auto set_response = ReadJsonFrame(fd);
  ASSERT_TRUE(set_response.has_value());
  EXPECT_EQ(set_response->at("txn_id"), 1);
  EXPECT_EQ(set_response->at("state"), "committed");
  ASSERT_TRUE(set_response->contains("operations"));
  ASSERT_EQ(set_response->at("operations").size(), 1);
  EXPECT_TRUE(set_response->at("operations")[0].at("success").get<bool>());

  nlohmann::json get_request;
  get_request["txn_id"] = 2;
  get_request["operations"] = nlohmann::json::array();
  get_request["operations"].push_back({{"type", "get"}, {"key", "alpha"}});

  ASSERT_TRUE(WriteFrame(fd, get_request));
  const auto get_response = ReadJsonFrame(fd);
  ASSERT_TRUE(get_response.has_value());
  EXPECT_EQ(get_response->at("txn_id"), 2);
  EXPECT_EQ(get_response->at("state"), "committed");
  ASSERT_EQ(get_response->at("operations").size(), 1);
  const auto& op = get_response->at("operations")[0];
  EXPECT_TRUE(op.at("success").get<bool>());
  ASSERT_TRUE(op.contains("value"));
  EXPECT_EQ(op.at("value").at("kind"), "string");
  EXPECT_EQ(op.at("value").at("data"), "bravo");

  ::close(fd);

  network.Stop();
  core_server.Stop();
}
