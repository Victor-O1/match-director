#include "md/core/latency_stats.hpp"

#include <gtest/gtest.h>

namespace {

using md::LatencyRecorder;

TEST(LatencyRecorder, EmptySummaryIsZero) {
  LatencyRecorder r(8);
  const auto s = r.summarize();
  EXPECT_EQ(s.count, 0U);
  EXPECT_EQ(s.max_ns, 0);
}

TEST(LatencyRecorder, PercentilesUseNearestRank) {
  LatencyRecorder r(100);
  for (int i = 100; i >= 1; --i) {  // insert out of order on purpose
    r.record(i);
  }
  const auto s = r.summarize();
  EXPECT_EQ(s.count, 100U);
  EXPECT_EQ(s.min_ns, 1);
  EXPECT_EQ(s.p50_ns, 50);
  EXPECT_EQ(s.p95_ns, 95);
  EXPECT_EQ(s.p99_ns, 99);
  EXPECT_EQ(s.max_ns, 100);
  EXPECT_DOUBLE_EQ(s.mean_ns, 50.5);
}

TEST(LatencyRecorder, DropsInsteadOfGrowingWhenFull) {
  LatencyRecorder r(3);
  for (int i = 0; i < 5; ++i) {
    r.record(10);
  }
  const auto s = r.summarize();
  EXPECT_EQ(s.count, 3U);
  EXPECT_EQ(s.dropped, 2U);
}

TEST(LatencyRecorder, ResetKeepsWorking) {
  LatencyRecorder r(2);
  r.record(1);
  r.record(2);
  r.record(3);
  r.reset();
  r.record(7);
  const auto s = r.summarize();
  EXPECT_EQ(s.count, 1U);
  EXPECT_EQ(s.dropped, 0U);
  EXPECT_EQ(s.p50_ns, 7);
}

}  // namespace
