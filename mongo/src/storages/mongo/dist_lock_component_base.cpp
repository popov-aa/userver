#include <storages/mongo/dist_lock_component_base.hpp>

#include <components/statistics_storage.hpp>
#include <dist_lock/dist_lock_settings.hpp>
#include <storages/mongo/component.hpp>
#include <utils/statistics/metadata.hpp>

namespace storages::mongo {

DistLockComponentBase::DistLockComponentBase(
    const components::ComponentConfig& component_config,
    const components::ComponentContext& component_context,
    storages::mongo::Collection collection)
    : components::LoggableComponentBase(component_config, component_context) {
  auto lock_name = component_config.ParseString("lockname");

  auto ttl = component_config.ParseDuration("lock-ttl");
  auto mongo_timeout = component_config.ParseDuration("mongo-timeout");
  auto optional_restart_delay =
      component_config.ParseOptionalDuration("restart-delay");
  const auto prolong_ratio = 10;

  if (mongo_timeout >= ttl / 2)
    throw std::runtime_error("mongo-timeout must be less than lock-ttl / 2");

  dist_lock::DistLockSettings settings{ttl / prolong_ratio, ttl / prolong_ratio,
                                       ttl, mongo_timeout};
  if (optional_restart_delay) {
    settings.worker_func_restart_delay = *optional_restart_delay;
  }

  auto strategy =
      std::make_shared<DistLockStrategy>(std::move(collection), lock_name);

  worker_ = std::make_unique<dist_lock::DistLockedWorker>(
      std::move(lock_name), [this]() { DoWork(); }, std::move(strategy),
      settings);

  auto& statistics_storage =
      component_context.FindComponent<components::StatisticsStorage>();
  statistics_holder_ = statistics_storage.GetStorage().RegisterExtender(
      "distlock." + component_config.Name(),
      [this](const ::utils::statistics::StatisticsRequest&) {
        return worker_->GetStatisticsJson();
      });
}

DistLockComponentBase::~DistLockComponentBase() {
  statistics_holder_.Unregister();
}

dist_lock::DistLockedWorker& DistLockComponentBase::GetWorker() {
  return *worker_;
}

void DistLockComponentBase::Start() { worker_->Start(); }

void DistLockComponentBase::Stop() { worker_->Stop(); }

}  // namespace storages::mongo
