#pragma once

#include "smart_factory/os_adapter.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace smart_factory {

struct ExecutorStats {
  std::size_t queueDepth{0};
  std::size_t queueCapacity{0};
  std::uint64_t accepted{0};
  std::uint64_t rejected{0};
  std::uint64_t completed{0};
};

class NonRealtimeExecutor {
 public:
  explicit NonRealtimeExecutor(std::size_t workers, std::size_t queueCapacity = 1024);
  ~NonRealtimeExecutor();
  NonRealtimeExecutor(const NonRealtimeExecutor&) = delete;
  NonRealtimeExecutor& operator=(const NonRealtimeExecutor&) = delete;

  bool submit(std::function<void()> task);
  void shutdown() noexcept;
  ExecutorStats stats() const;

 private:
  void workerLoop();
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> queue_;
  std::vector<std::thread> workers_;
  std::size_t queueCapacity_{1024};
  std::uint64_t accepted_{0};
  std::uint64_t rejected_{0};
  std::uint64_t completed_{0};
  bool stopping_{false};
};

class RealtimeExecutor {
 public:
  RealtimeExecutor(std::unique_ptr<OsAdapter> osAdapter, RealtimeThreadOptions options,
                   std::size_t queueCapacity = 256);
  ~RealtimeExecutor();
  RealtimeExecutor(const RealtimeExecutor&) = delete;
  RealtimeExecutor& operator=(const RealtimeExecutor&) = delete;

  bool submit(std::function<void()> task);
  void shutdown() noexcept;
  bool realtimeGranted() const noexcept { return realtimeGranted_.load(); }
  std::string realtimeDetail() const;
  std::string osAdapterName() const;
  ExecutorStats stats() const;

 private:
  void loop();
  std::unique_ptr<OsAdapter> osAdapter_;
  RealtimeThreadOptions options_;
  mutable std::mutex statusMutex_;
  std::string realtimeDetail_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> queue_;
  std::size_t queueCapacity_{256};
  std::uint64_t accepted_{0};
  std::uint64_t rejected_{0};
  std::uint64_t completed_{0};
  std::thread worker_;
  std::atomic<bool> realtimeGranted_{false};
  bool stopping_{false};
};

}  // namespace smart_factory
