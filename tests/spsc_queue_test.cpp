#include "md/core/spsc_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <thread>

namespace {

using md::SpscQueue;

TEST(SpscQueue, StartsEmpty) {
  auto q = std::make_unique<SpscQueue<int, 4>>();
  int out = -1;
  EXPECT_FALSE(q->try_pop(out));
  EXPECT_EQ(out, -1);
  EXPECT_EQ(q->size_approx(), 0U);
}

TEST(SpscQueue, FifoOrderAndFullDetection) {
  auto q = std::make_unique<SpscQueue<int, 4>>();
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(q->try_push(int{i}));
  }
  EXPECT_FALSE(q->try_push(99)) << "fifth push must fail on a capacity-4 queue";
  EXPECT_EQ(q->size_approx(), 4U);

  for (int i = 0; i < 4; ++i) {
    int out = -1;
    ASSERT_TRUE(q->try_pop(out));
    EXPECT_EQ(out, i);
  }
  int out = -1;
  EXPECT_FALSE(q->try_pop(out));
}

TEST(SpscQueue, WrapsAroundManyTimes) {
  auto q = std::make_unique<SpscQueue<int, 8>>();
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(q->try_push(int{i}));
    ASSERT_TRUE(q->try_push(int{i + 1}));
    int a = 0;
    int b = 0;
    ASSERT_TRUE(q->try_pop(a));
    ASSERT_TRUE(q->try_pop(b));
    EXPECT_EQ(a, i);
    EXPECT_EQ(b, i + 1);
  }
}

TEST(SpscQueue, MovesOwnershipThrough) {
  auto q = std::make_unique<SpscQueue<std::unique_ptr<int>, 2>>();
  ASSERT_TRUE(q->try_push(std::make_unique<int>(42)));
  std::unique_ptr<int> out;
  ASSERT_TRUE(q->try_pop(out));
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(*out, 42);
}

// Real producer and consumer threads. Under TSAN this is the test that proves
// the acquire/release pairing is correct: any missing ordering is a report.
TEST(SpscQueue, TwoThreadsTransferEveryItemInOrder) {
  constexpr std::uint64_t kItems = 1'000'000;
  auto q = std::make_unique<SpscQueue<std::uint64_t, 1024>>();

  std::thread producer([&q] {
    for (std::uint64_t i = 0; i < kItems; ++i) {
      std::uint64_t v = i;
      while (!q->try_push(std::move(v))) {
        std::this_thread::yield();
      }
    }
  });

  std::uint64_t expected = 0;
  std::uint64_t out = 0;
  while (expected < kItems) {
    if (q->try_pop(out)) {
      ASSERT_EQ(out, expected);
      ++expected;
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();
  EXPECT_FALSE(q->try_pop(out));
}

}  // namespace
