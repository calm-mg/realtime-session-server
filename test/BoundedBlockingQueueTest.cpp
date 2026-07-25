#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>

#include "rss/util/BoundedBlockingQueue.h"

namespace {

using namespace std::chrono_literals;
using Queue = rss::util::BoundedBlockingQueue<int>;
constexpr auto kWakeupTimeout = 2s;

template <typename Predicate>
bool waitUntil(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kWakeupTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::yield();
  }
  return predicate();
}

TEST(BoundedBlockingQueueTest, TryPushStopsAtCapacityAndKeepsFifoOrder) {
  Queue queue(2);

  const auto first_push = queue.tryPush(10);
  const auto second_push = queue.tryPush(20);
  const auto rejected_push = queue.tryPush(30);

  EXPECT_TRUE(first_push.succeeded);
  EXPECT_EQ(first_push.size, 1U);
  EXPECT_TRUE(second_push.succeeded);
  EXPECT_EQ(second_push.size, 2U);
  EXPECT_FALSE(rejected_push.succeeded);
  EXPECT_EQ(rejected_push.size, 2U);

  const auto first_pop = queue.tryPop();
  const auto second_pop = queue.tryPop();
  const auto empty_pop = queue.tryPop();

  ASSERT_TRUE(first_pop.value.has_value());
  EXPECT_EQ(*first_pop.value, 10);
  EXPECT_EQ(first_pop.size, 1U);
  ASSERT_TRUE(second_pop.value.has_value());
  EXPECT_EQ(*second_pop.value, 20);
  EXPECT_EQ(second_pop.size, 0U);
  EXPECT_FALSE(empty_pop.value.has_value());
  EXPECT_EQ(empty_pop.size, 0U);
}

TEST(BoundedBlockingQueueTest, RejectsZeroCapacity) {
  EXPECT_THROW((Queue{0}), std::invalid_argument);
}

TEST(BoundedBlockingQueueTest, PushWaitsUntilPopCreatesCapacity) {
  Queue queue(1);
  ASSERT_TRUE(queue.tryPush(10).succeeded);

  std::promise<Queue::OperationResult> result_promise;
  auto result = result_promise.get_future();
  std::thread producer([&] { result_promise.set_value(queue.push(20)); });

  const bool producer_is_waiting = waitUntil(
      [&] { return queue.waiterCounts().producers == 1U; });
  EXPECT_TRUE(producer_is_waiting);
  if (!producer_is_waiting) {
    queue.close();
    producer.join();
    EXPECT_EQ(result.wait_for(0ms), std::future_status::ready);
    return;
  }

  EXPECT_EQ(result.wait_for(0ms), std::future_status::timeout);

  int popped_value = 0;
  EXPECT_TRUE(queue.pop(popped_value).succeeded);
  EXPECT_EQ(popped_value, 10);

  const auto completion_status = result.wait_for(kWakeupTimeout);
  if (completion_status != std::future_status::ready) {
    queue.close();
  }
  EXPECT_EQ(completion_status, std::future_status::ready);
  producer.join();

  if (result.wait_for(0ms) != std::future_status::ready) {
    ADD_FAILURE() << "producer result was not ready after cleanup";
    return;
  }
  const auto push_result = result.get();

  EXPECT_TRUE(push_result.succeeded);
  EXPECT_EQ(push_result.size, 1U);
  EXPECT_EQ(queue.waiterCounts().producers, 0U);
}

TEST(BoundedBlockingQueueTest, PopWaitsUntilPushProvidesValue) {
  Queue queue(1);

  std::promise<std::pair<Queue::OperationResult, int>> result_promise;
  auto result = result_promise.get_future();
  std::thread consumer([&] {
    int popped_value = 0;
    result_promise.set_value({queue.pop(popped_value), popped_value});
  });

  const bool consumer_is_waiting = waitUntil(
      [&] { return queue.waiterCounts().consumers == 1U; });
  EXPECT_TRUE(consumer_is_waiting);
  if (!consumer_is_waiting) {
    queue.close();
    consumer.join();
    EXPECT_EQ(result.wait_for(0ms), std::future_status::ready);
    return;
  }

  EXPECT_EQ(result.wait_for(0ms), std::future_status::timeout);
  EXPECT_TRUE(queue.push(30).succeeded);

  const auto completion_status = result.wait_for(kWakeupTimeout);
  if (completion_status != std::future_status::ready) {
    queue.close();
  }
  EXPECT_EQ(completion_status, std::future_status::ready);
  consumer.join();

  if (result.wait_for(0ms) != std::future_status::ready) {
    ADD_FAILURE() << "consumer result was not ready after cleanup";
    return;
  }
  const auto [pop_result, popped_value] = result.get();

  EXPECT_TRUE(pop_result.succeeded);
  EXPECT_EQ(pop_result.size, 0U);
  EXPECT_EQ(popped_value, 30);
  EXPECT_EQ(queue.waiterCounts().consumers, 0U);
}

TEST(BoundedBlockingQueueTest, CloseWakesBlockedProducerAndReportsFailure) {
  Queue queue(1);
  ASSERT_TRUE(queue.tryPush(10).succeeded);

  std::promise<Queue::OperationResult> result_promise;
  auto result = result_promise.get_future();
  std::thread producer([&] { result_promise.set_value(queue.push(20)); });

  const bool producer_is_waiting = waitUntil(
      [&] { return queue.waiterCounts().producers == 1U; });
  EXPECT_TRUE(producer_is_waiting);
  if (!producer_is_waiting) {
    queue.close();
    producer.join();
    EXPECT_EQ(result.wait_for(0ms), std::future_status::ready);
    return;
  }

  EXPECT_EQ(result.wait_for(0ms), std::future_status::timeout);
  queue.close();

  const auto completion_status = result.wait_for(kWakeupTimeout);
  EXPECT_EQ(completion_status, std::future_status::ready);
  if (completion_status != std::future_status::ready) {
    queue.close();
  }
  producer.join();

  if (result.wait_for(0ms) != std::future_status::ready) {
    ADD_FAILURE() << "producer result was not ready after cleanup";
    return;
  }
  const auto push_result = result.get();

  EXPECT_FALSE(push_result.succeeded);
  EXPECT_EQ(push_result.size, 1U);
  EXPECT_EQ(queue.waiterCounts().producers, 0U);
}

TEST(BoundedBlockingQueueTest, CloseWakesBlockedConsumerAndReportsFailure) {
  Queue queue(1);

  std::promise<Queue::OperationResult> result_promise;
  auto result = result_promise.get_future();
  std::thread consumer([&] {
    int popped_value = 0;
    result_promise.set_value(queue.pop(popped_value));
  });

  const bool consumer_is_waiting = waitUntil(
      [&] { return queue.waiterCounts().consumers == 1U; });
  EXPECT_TRUE(consumer_is_waiting);
  if (!consumer_is_waiting) {
    queue.close();
    consumer.join();
    EXPECT_EQ(result.wait_for(0ms), std::future_status::ready);
    return;
  }

  EXPECT_EQ(result.wait_for(0ms), std::future_status::timeout);
  queue.close();

  const auto completion_status = result.wait_for(kWakeupTimeout);
  EXPECT_EQ(completion_status, std::future_status::ready);
  if (completion_status != std::future_status::ready) {
    queue.close();
  }
  consumer.join();

  if (result.wait_for(0ms) != std::future_status::ready) {
    ADD_FAILURE() << "consumer result was not ready after cleanup";
    return;
  }
  const auto pop_result = result.get();

  EXPECT_FALSE(pop_result.succeeded);
  EXPECT_EQ(pop_result.size, 0U);
  EXPECT_EQ(queue.waiterCounts().consumers, 0U);
}

TEST(BoundedBlockingQueueTest, CloseDrainsQueuedValuesBeforePopFails) {
  Queue queue(2);
  ASSERT_TRUE(queue.push(10).succeeded);
  ASSERT_TRUE(queue.push(20).succeeded);
  queue.close();

  int first_value = 0;
  int second_value = 0;
  int ignored_value = 0;
  const auto first_pop = queue.pop(first_value);
  const auto second_pop = queue.pop(second_value);
  const auto failed_pop = queue.pop(ignored_value);

  EXPECT_TRUE(first_pop.succeeded);
  EXPECT_EQ(first_pop.size, 1U);
  EXPECT_EQ(first_value, 10);
  EXPECT_TRUE(second_pop.succeeded);
  EXPECT_EQ(second_pop.size, 0U);
  EXPECT_EQ(second_value, 20);
  EXPECT_FALSE(failed_pop.succeeded);
  EXPECT_EQ(failed_pop.size, 0U);
}

}  // namespace
