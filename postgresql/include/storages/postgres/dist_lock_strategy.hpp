#pragma once

#include <dist_lock/dist_lock_settings.hpp>
#include <dist_lock/dist_lock_strategy.hpp>
#include <engine/deadline.hpp>
#include <rcu/rcu.hpp>
#include <storages/postgres/options.hpp>

namespace storages {
namespace postgres {

class DistLockStrategy final : public dist_lock::DistLockStrategyBase {
 public:
  DistLockStrategy(ClusterPtr cluster, const std::string& table,
                   const std::string& lock_name,
                   const dist_lock::DistLockSettings& settings);

  void Acquire(std::chrono::milliseconds) override;

  void Release() override;

  void UpdateCommandControl(CommandControl cc);

 private:
  ClusterPtr cluster_;
  rcu::Variable<CommandControl> cc_;
  const std::string acquire_query_;
  const std::string release_query_;
  const std::string lock_name_;
  const std::string owner_;
};

}  // namespace postgres
}  // namespace storages
