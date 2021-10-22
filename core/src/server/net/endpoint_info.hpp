#pragma once

#include <atomic>

#include <server/http/http_request_handler.hpp>
#include <server/net/connection.hpp>

#include "listener_config.hpp"

USERVER_NAMESPACE_BEGIN

namespace server {
namespace net {

struct EndpointInfo {
  EndpointInfo(const ListenerConfig&, http::HttpRequestHandler&);

  std::string GetDescription() const;

  const ListenerConfig& listener_config;
  http::HttpRequestHandler& request_handler;
  Connection::Type connection_type{Connection::Type::kRequest};

  std::atomic<size_t> connection_count{0};
};

}  // namespace net
}  // namespace server

USERVER_NAMESPACE_END
