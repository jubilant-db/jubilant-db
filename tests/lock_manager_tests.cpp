#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "lock/lock_manager.h"

using jubilant::lock::LockManager;
using jubilant::lock::LockMode;

namespace {

TEST(LockManagerTest, AllowsConcurrentSharedLocks) {
  LockManager manager;
  std::atomic<int> active_shared{0};
  std::atomic<int> peak_shared{0};

  auto update_max = [](std::atomic<int>& target, int value) {
    int observed = target.load(std::memory_order_relaxed);
    while (value > observed &&
           !target.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
    }
  };

  auto shared_worker = [&]() {
    manager.Acquire("resource", LockMode::kShared);
    const int current = ++active_shared;
    update_max(peak_shared, current);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    --active_shared;
    manager.Release("resource", LockMode::kShared);
  };

  std::thread first(shared_worker);
  std::thread second(shared_worker);

  first.join();
  second.join();

  EXPECT_EQ(peak_shared.load(std::memory_order_relaxed), 2);
}

TEST(LockManagerTest, ExclusiveBlocksShared) {
  LockManager manager;

  std::chrono::steady_clock::time_point shared_acquired{};

  std::thread exclusive_holder([&]() {
    manager.Acquire("resource", LockMode::kExclusive);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    manager.Release("resource", LockMode::kExclusive);
  });

  std::thread shared_waiter([&]() {
    const auto start = std::chrono::steady_clock::now();
    manager.Acquire("resource", LockMode::kShared);
    shared_acquired = std::chrono::steady_clock::now();
    manager.Release("resource", LockMode::kShared);

    const auto wait_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(shared_acquired - start);
    EXPECT_GE(wait_time.count(), 70);
  });

  exclusive_holder.join();
  shared_waiter.join();
}

TEST(LockManagerTest, ExclusiveAccessIsSerialized) {
  LockManager manager;
  std::atomic<int> active_exclusive{0};
  std::atomic<int> peak_exclusive{0};

  auto update_max = [](std::atomic<int>& target, int value) {
    int observed = target.load(std::memory_order_relaxed);
    while (value > observed &&
           !target.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
    }
  };

  auto exclusive_worker = [&]() {
    manager.Acquire("resource", LockMode::kExclusive);
    const int current = ++active_exclusive;
    update_max(peak_exclusive, current);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    --active_exclusive;
    manager.Release("resource", LockMode::kExclusive);
  };

  std::vector<std::thread> workers;
  for (int i = 0; i < 3; ++i) {
    workers.emplace_back(exclusive_worker);
  }

  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(peak_exclusive.load(std::memory_order_relaxed), 1);
}

}  // namespace
