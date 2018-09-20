#include <server/server.hpp>

#include <stdexcept>

#include <logging/log.hpp>
#include <server/server_config.hpp>

#include <engine/task/task_processor.hpp>
#include <server/net/endpoint_info.hpp>
#include <server/net/listener.hpp>
#include <server/net/stats.hpp>
#include <server/request_handlers/request_handlers.hpp>

namespace {

const char* PER_LISTENER_DESC = "per-listener";
const char* PER_CONNECTION_DESC = "per-connection";

using Verbosity = components::MonitorVerbosity;

formats::json::ValueBuilder SerializeAggregated(
    const server::net::Stats::AggregatedStat& agg, Verbosity verbosity,
    const char* items_desc) {
  formats::json::ValueBuilder json_agg(formats::json::Type::kObject);
  json_agg["total"] = agg.Total();
  json_agg["max"] = agg.Max();
  if (verbosity == Verbosity::kFull) {
    formats::json::ValueBuilder json_items(formats::json::Type::kArray);
    for (auto item : agg.Items()) {
      json_items.PushBack(item);
    }
    json_agg[items_desc] = std::move(json_items);
  }
  return json_agg;
}

}  // namespace

namespace server {

class ServerImpl {
 public:
  ServerImpl(ServerConfig config,
             const components::ComponentContext& component_context);
  ~ServerImpl();

  net::Stats GetServerStats() const;
  std::unique_ptr<RequestHandlers> CreateRequestHandlers(
      const components::ComponentContext& component_context) const;

  const ServerConfig config_;

  std::unique_ptr<RequestHandlers> request_handlers_;
  std::shared_ptr<net::EndpointInfo> endpoint_info_;

  mutable std::shared_timed_mutex stat_mutex_;
  std::vector<net::Listener> listeners_;
  bool is_destroying_;
};

ServerImpl::ServerImpl(ServerConfig config,
                       const components::ComponentContext& component_context)
    : config_(std::move(config)), is_destroying_(false) {
  LOG_INFO() << "Creating server";

  engine::TaskProcessor* task_processor =
      component_context.GetTaskProcessor(config_.task_processor);
  if (!task_processor) {
    throw std::runtime_error("can't find task_processor '" +
                             config_.task_processor + "' for server");
  }

  request_handlers_ = CreateRequestHandlers(component_context);

  endpoint_info_ =
      std::make_shared<net::EndpointInfo>(config_.listener, *request_handlers_);

  auto& event_thread_pool = task_processor->EventThreadPool();
  size_t listener_shards = config_.listener.shards ? *config_.listener.shards
                                                   : event_thread_pool.size();
  auto event_thread_controls = event_thread_pool.NextThreads(listener_shards);
  for (auto* event_thread_control : event_thread_controls) {
    listeners_.emplace_back(endpoint_info_, *task_processor,
                            *event_thread_control);
  }

  LOG_INFO() << "Server is created";
}

ServerImpl::~ServerImpl() {
  {
    std::unique_lock<std::shared_timed_mutex> lock(stat_mutex_);
    is_destroying_ = true;
  }
  LOG_INFO() << "Stopping server";
  LOG_TRACE() << "Stopping listeners";
  listeners_.clear();
  LOG_TRACE() << "Stopped listeners";
  LOG_TRACE() << "Stopping request handlers";
  request_handlers_.reset();
  LOG_TRACE() << "Stopped request handlers";
  LOG_INFO() << "Stopped server";
}

Server::Server(ServerConfig config,
               const components::ComponentContext& component_context)
    : pimpl(
          std::make_unique<ServerImpl>(std::move(config), component_context)) {}

Server::~Server() = default;

const ServerConfig& Server::GetConfig() const { return pimpl->config_; }

formats::json::Value Server::GetMonitorData(
    components::MonitorVerbosity verbosity) const {
  formats::json::ValueBuilder json_data(formats::json::Type::kObject);

  auto server_stats = pimpl->GetServerStats();
  {
    formats::json::ValueBuilder json_conn_stats(formats::json::Type::kObject);
    json_conn_stats["active"] = SerializeAggregated(
        server_stats.active_connections, verbosity, PER_LISTENER_DESC);
    json_conn_stats["opened"] = SerializeAggregated(
        server_stats.total_opened_connections, verbosity, PER_LISTENER_DESC);
    json_conn_stats["closed"] = SerializeAggregated(
        server_stats.total_closed_connections, verbosity, PER_LISTENER_DESC);

    json_data["connections"] = std::move(json_conn_stats);
  }
  {
    formats::json::ValueBuilder json_request_stats(
        formats::json::Type::kObject);
    json_request_stats["active"] = SerializeAggregated(
        server_stats.active_requests, verbosity, PER_CONNECTION_DESC);
    json_request_stats["parsing"] = SerializeAggregated(
        server_stats.parsing_requests, verbosity, PER_CONNECTION_DESC);
    json_request_stats["pending-response"] = SerializeAggregated(
        server_stats.pending_responses, verbosity, PER_CONNECTION_DESC);
    json_request_stats["conn-processed"] = SerializeAggregated(
        server_stats.conn_processed_requests, verbosity, PER_CONNECTION_DESC);
    json_request_stats["listener-processed"] = SerializeAggregated(
        server_stats.listener_processed_requests, verbosity, PER_LISTENER_DESC);

    json_data["requests"] = std::move(json_request_stats);
  }

  return json_data.ExtractValue();
}

bool Server::AddHandler(const handlers::HandlerBase& handler,
                        const components::ComponentContext& component_context) {
  return (handler.IsMonitor()
              ? pimpl->request_handlers_->GetMonitorRequestHandler()
              : pimpl->request_handlers_->GetHttpRequestHandler())
      .AddHandler(handler, component_context);
}

void Server::Start() {
  LOG_INFO() << "Starting server";
  pimpl->request_handlers_->GetMonitorRequestHandler().DisableAddHandler();
  pimpl->request_handlers_->GetHttpRequestHandler().DisableAddHandler();
  for (auto& listener : pimpl->listeners_) {
    listener.Start();
  }
  LOG_INFO() << "Server is started";
}

net::Stats ServerImpl::GetServerStats() const {
  net::Stats summary;

  std::shared_lock<std::shared_timed_mutex> lock(stat_mutex_);
  if (is_destroying_) return summary;
  for (const auto& listener : listeners_) {
    summary += listener.GetStats();
  }
  return summary;
}

std::unique_ptr<RequestHandlers> ServerImpl::CreateRequestHandlers(
    const components::ComponentContext& component_context) const {
  auto request_handlers = std::make_unique<RequestHandlers>();
  try {
    request_handlers->SetHttpRequestHandler(
        std::make_unique<http::HttpRequestHandler>(
            component_context, config_.logger_access,
            config_.logger_access_tskv, false));
  } catch (const std::exception& ex) {
    LOG_ERROR() << "can't create HttpRequestHandler: " << ex.what();
    throw;
  }
  try {
    request_handlers->SetMonitorRequestHandler(
        std::make_unique<http::HttpRequestHandler>(
            component_context, config_.logger_access,
            config_.logger_access_tskv, true));
  } catch (const std::exception& ex) {
    LOG_ERROR() << "can't create MonitorRequestHandler: " << ex.what();
    throw;
  }
  return request_handlers;
}

}  // namespace server
