#include "smart_factory/executor.hpp"

#include <algorithm>
#include <stdexcept>

namespace smart_factory {

NonRealtimeExecutor::NonRealtimeExecutor(std::size_t workers, std::size_t queueCapacity)
    : queueCapacity_(std::max<std::size_t>(1, queueCapacity)) {
  workers = std::max<std::size_t>(1, workers);
  workers_.reserve(workers);
  for (std::size_t i = 0; i < workers; ++i) workers_.emplace_back([this] { workerLoop(); });
}

NonRealtimeExecutor::~NonRealtimeExecutor() { shutdown(); }

void NonRealtimeExecutor::shutdown() noexcept {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& worker : workers_) if (worker.joinable()) worker.join();
}

bool NonRealtimeExecutor::submit(std::function<void()> task) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_ || queue_.size() >= queueCapacity_) {
      ++rejected_;
      return false;
    }
    queue_.push(std::move(task));
    ++accepted_;
  }
  cv_.notify_one();
  return true;
}

ExecutorStats NonRealtimeExecutor::stats() const {
  std::lock_guard lock(mutex_);
  return {queue_.size(), queueCapacity_, accepted_, rejected_, completed_};
}

void NonRealtimeExecutor::workerLoop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop();
    }
    try { task(); } catch (...) { /* isolate task failures from worker lifetime */ }
    {
      std::lock_guard lock(mutex_);
      ++completed_;
    }
  }
}

RealtimeExecutor::RealtimeExecutor(std::unique_ptr<OsAdapter> osAdapter, RealtimeThreadOptions options,
                                   std::size_t queueCapacity)
    : osAdapter_(std::move(osAdapter)), options_(std::move(options)),
      queueCapacity_(std::max<std::size_t>(1, queueCapacity)), worker_([this] { loop(); }) {
  if (!osAdapter_) throw std::runtime_error("RealtimeExecutor requires OsAdapter");
}

RealtimeExecutor::~RealtimeExecutor() { shutdown(); }

void RealtimeExecutor::shutdown() noexcept {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool RealtimeExecutor::submit(std::function<void()> task) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_ || queue_.size() >= queueCapacity_) {
      ++rejected_;
      return false;
    }
    queue_.push(std::move(task));
    ++accepted_;
  }
  cv_.notify_one();
  return true;
}

ExecutorStats RealtimeExecutor::stats() const {
  std::lock_guard lock(mutex_);
  return {queue_.size(), queueCapacity_, accepted_, rejected_, completed_};
}

std::string RealtimeExecutor::realtimeDetail() const {
  std::lock_guard lock(statusMutex_);
  return realtimeDetail_;
}

std::string RealtimeExecutor::osAdapterName() const { return osAdapter_ ? osAdapter_->name() : "none"; }

void RealtimeExecutor::loop() {
  if (osAdapter_) {
    const auto grant = osAdapter_->configureCurrentThreadRealtime(options_);
    realtimeGranted_.store(grant.granted);
    std::lock_guard lock(statusMutex_);
    realtimeDetail_ = grant.detail;
  }
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop();
    }
    try { task(); } catch (...) { /* isolate task failure */ }
    {
      std::lock_guard lock(mutex_);
      ++completed_;
    }
  }
}

}  // namespace smart_factory
