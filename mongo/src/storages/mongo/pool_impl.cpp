#include <storages/mongo/pool_impl.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::mongo::impl {

PoolImpl::PoolImpl(std::string&& id) : id_(std::move(id)) {}

const std::string& PoolImpl::Id() const { return id_; }

const stats::PoolStatistics& PoolImpl::GetStatistics() const {
  return statistics_;
}

stats::PoolStatistics& PoolImpl::GetStatistics() { return statistics_; }

}  // namespace storages::mongo::impl

USERVER_NAMESPACE_END
