#pragma once

#include <algorithm>  // for std::max
#include <atomic>
#include <cerrno>
#include <utility>

#include <moodycamel/concurrentqueue.h>

#include <coroutines/coroutine.hpp>

#include <userver/logging/log.hpp>
#include <userver/utils/assert.hpp>

#include <utils/sys_info.hpp>

#include "pool_config.hpp"
#include "pool_stats.hpp"
#include "stack_usage_monitor.hpp"

USERVER_NAMESPACE_BEGIN

namespace engine::coro {

template <typename Task>
class Pool final {
 public:
  using Coroutine = typename boost::coroutines2::coroutine<Task*>::push_type;
  class CoroutinePtr;
  using TaskPipe = typename boost::coroutines2::coroutine<Task*>::pull_type;
  using Executor = void (*)(TaskPipe&);

  Pool(PoolConfig config, Executor executor);
  ~Pool();

  CoroutinePtr GetCoroutine();
  void PutCoroutine(CoroutinePtr&& coroutine_ptr);
  PoolStats GetStats() const;
  std::size_t GetStackSize() const;

  void RegisterThread();
  void AccountStackUsage();

 private:
  static PoolConfig FixupConfig(PoolConfig&& config);

  Coroutine CreateCoroutine(bool quiet = false);
  void OnCoroutineDestruction() noexcept;

  template <typename Token>
  Token& GetUsedPoolToken();

  const PoolConfig config_;
  const Executor executor_;

  boost::coroutines2::protected_fixedsize_stack stack_allocator_;
  // Some pointers arithmetic in StackUsageMonitor depends on this.
  // If you change the allocator, adjust the math there accordingly.
  static_assert(std::is_same_v<decltype(stack_allocator_),
                               boost::coroutines2::protected_fixedsize_stack>);
  StackUsageMonitor stack_usage_monitor_;

  // We aim to reuse coroutines as much as possible,
  // because since coroutine stack is a mmap-ed chunk of memory and not actually
  // an allocated memory we don't want to de-virtualize that memory excessively.
  //
  // The same could've been achieved with some LIFO container, but apparently
  // we don't have a container handy enough to not just use 2 queues.
  moodycamel::ConcurrentQueue<Coroutine> initial_coroutines_;
  moodycamel::ConcurrentQueue<Coroutine> used_coroutines_;

  std::atomic<std::size_t> idle_coroutines_num_;
  std::atomic<std::size_t> total_coroutines_num_;
};

template <typename Task>
class Pool<Task>::CoroutinePtr final {
 public:
  CoroutinePtr(Coroutine&& coro, Pool<Task>& pool) noexcept
      : coro_(std::move(coro)), pool_(&pool) {}

  CoroutinePtr(CoroutinePtr&&) noexcept = default;
  CoroutinePtr& operator=(CoroutinePtr&&) noexcept = default;

  ~CoroutinePtr() {
    UASSERT(pool_);
    if (coro_) pool_->OnCoroutineDestruction();
  }

  Coroutine& Get() noexcept {
    UASSERT(coro_);
    return coro_;
  }

  void ReturnToPool() && {
    UASSERT(coro_);
    pool_->PutCoroutine(std::move(*this));
  }

 private:
  Coroutine coro_;
  Pool<Task>* pool_;
};

template <typename Task>
Pool<Task>::Pool(PoolConfig config, Executor executor)
    : config_(FixupConfig(std::move(config))),
      executor_(executor),
      stack_allocator_(config_.stack_size),
      stack_usage_monitor_(config_.stack_size),
      initial_coroutines_(config_.initial_size),
      used_coroutines_(config_.max_size),
      idle_coroutines_num_(config_.initial_size),
      total_coroutines_num_(0) {
  moodycamel::ProducerToken token(initial_coroutines_);

  stack_usage_monitor_.Start();

  for (std::size_t i = 0; i < config_.initial_size; ++i) {
    bool ok =
        initial_coroutines_.enqueue(token, CreateCoroutine(/*quiet =*/true));
    UINVARIANT(ok, "Failed to allocate the initial coro pool");
  }
}

template <typename Task>
Pool<Task>::~Pool() = default;

template <typename Task>
typename Pool<Task>::CoroutinePtr Pool<Task>::GetCoroutine() {
  struct CoroutineMover {
    std::optional<Coroutine>& result;

    CoroutineMover& operator=(Coroutine&& coro) {
      result.emplace(std::move(coro));
      return *this;
    }
  };

  std::optional<Coroutine> coroutine;
  CoroutineMover mover{coroutine};

  // First try to dequeue from 'working set': if we can get a coroutine
  // from there we are happy, because we saved on minor-page-faulting (thus
  // increasing resident memory usage) a not-yet-de-virtualized coroutine stack.
  if (used_coroutines_.try_dequeue(
          GetUsedPoolToken<moodycamel::ConsumerToken>(), mover) ||
      initial_coroutines_.try_dequeue(mover)) {
    --idle_coroutines_num_;
  } else {
    coroutine.emplace(CreateCoroutine());
  }
  return CoroutinePtr(std::move(*coroutine), *this);
}

template <typename Task>
void Pool<Task>::PutCoroutine(CoroutinePtr&& coroutine_ptr) {
  if (idle_coroutines_num_.load() >= config_.max_size) return;
  auto& token = GetUsedPoolToken<moodycamel::ProducerToken>();
  const bool ok =
      // We only ever return coroutines into our 'working set'.
      used_coroutines_.enqueue(token, std::move(coroutine_ptr.Get()));
  if (ok) ++idle_coroutines_num_;
}

template <typename Task>
PoolStats Pool<Task>::GetStats() const {
  PoolStats stats;
  stats.active_coroutines =
      total_coroutines_num_.load() -
      (used_coroutines_.size_approx() + initial_coroutines_.size_approx());
  stats.total_coroutines =
      std::max(total_coroutines_num_.load(), stats.active_coroutines);
  stats.max_stack_usage_pct = stack_usage_monitor_.GetMaxStackUsagePct();
  stats.is_stack_usage_monitor_active = stack_usage_monitor_.IsActive();
  return stats;
}

template <typename Task>
typename Pool<Task>::Coroutine Pool<Task>::CreateCoroutine(bool quiet) {
  try {
    Coroutine coroutine(stack_allocator_, executor_);
    const auto new_total = ++total_coroutines_num_;
    if (!quiet) {
      LOG_DEBUG() << "Created a coroutine #" << new_total << '/'
                  << config_.max_size;
    }

    stack_usage_monitor_.Register(coroutine);

    return coroutine;
  } catch (const std::bad_alloc&) {
    if (errno == ENOMEM) {
      // It should be ok to allocate here (which LOG_ERROR might do),
      // because ENOMEM is most likely coming from mmap
      // hitting vm.max_map_count limit, not from the actual memory limit.
      // See `stack_context::allocate` in
      // boost/context/posix/protected_fixedsize_stack.hpp
      LOG_ERROR() << "Failed to allocate a coroutine (ENOMEM), current "
                     "coroutines count: "
                  << total_coroutines_num_.load()
                  << "; are you hitting the vm.max_map_count limit?";
    }

    throw;
  }
}

template <typename Task>
void Pool<Task>::OnCoroutineDestruction() noexcept {
  --total_coroutines_num_;
}

template <typename Task>
std::size_t Pool<Task>::GetStackSize() const {
  return config_.stack_size;
}

template <typename Task>
PoolConfig Pool<Task>::FixupConfig(PoolConfig&& config) {
  const auto page_size = utils::sys_info::GetPageSize();
  config.stack_size = (config.stack_size + page_size - 1) & ~(page_size - 1);

  return std::move(config);
}

template <typename Task>
void Pool<Task>::RegisterThread() {
  stack_usage_monitor_.RegisterThread();
}

template <typename Task>
void Pool<Task>::AccountStackUsage() {
  stack_usage_monitor_.AccountStackUsage();
}

template <typename Task>
template <typename Token>
Token& Pool<Task>::GetUsedPoolToken() {
  thread_local Token token(used_coroutines_);
  return token;
}

}  // namespace engine::coro

USERVER_NAMESPACE_END
