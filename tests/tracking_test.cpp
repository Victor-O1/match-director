#include "md/track/hungarian.hpp"
#include "md/track/tracker.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

namespace {

using md::Detection;
using md::HungarianSolver;
using md::Tracker;

double brute_force_min(const std::vector<double>& c, std::size_t n) {
  std::vector<std::size_t> perm(n);
  std::iota(perm.begin(), perm.end(), 0);
  double best = 1e18;
  do {
    double s = 0;
    for (std::size_t r = 0; r < n; ++r) s += c[r * n + perm[r]];
    best = std::min(best, s);
  } while (std::next_permutation(perm.begin(), perm.end()));
  return best;
}

TEST(Hungarian, KnownSmallCase) {
  // Classic example: optimal = 0->1, 1->0, 2->2, total 1 + 2 + 2 = 5.
  const std::vector<double> c = {4, 1, 3, 2, 0, 5, 3, 2, 2};
  HungarianSolver s;
  const auto& m = s.solve(c, 3, 3);
  EXPECT_EQ(m[0], 1);
  EXPECT_EQ(m[1], 0);
  EXPECT_EQ(m[2], 2);
}

TEST(Hungarian, MatchesBruteForceOnRandomMatrices) {
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> d(0.0, 100.0);
  HungarianSolver s;
  for (int trial = 0; trial < 200; ++trial) {
    const std::size_t n = 1 + static_cast<std::size_t>(trial % 6);
    std::vector<double> c(n * n);
    for (double& v : c) v = d(rng);
    const auto& m = s.solve(c, n, n);
    double total = 0;
    std::vector<bool> used(n, false);
    for (std::size_t r = 0; r < n; ++r) {
      ASSERT_GE(m[r], 0);
      const auto col = static_cast<std::size_t>(m[r]);
      ASSERT_FALSE(used[col]) << "column assigned twice";
      used[col] = true;
      total += c[r * n + col];
    }
    EXPECT_NEAR(total, brute_force_min(c, n), 1e-9);
  }
}

TEST(Hungarian, RectangularMoreRowsThanCols) {
  const std::vector<double> c = {1, 9, 9, 1, 5, 5};  // 3 rows, 2 cols
  HungarianSolver s;
  const auto& m = s.solve(c, 3, 2);
  EXPECT_EQ(m[0], 0);
  EXPECT_EQ(m[1], 1);
  EXPECT_EQ(m[2], -1);
}

Detection det_at(double x, double y) {
  Detection d;
  d.foot = {x, y};
  d.box = {x - 5, y - 30, 10, 30};
  d.area = 50;
  return d;
}

TEST(Kalman1D, ConvergesToConstantVelocity) {
  md::Kalman1D k;
  k.pos = 0;
  for (int i = 1; i <= 100; ++i) {
    k.predict(0.04, 60.0);
    k.update(i * 4.0, 2.0);  // 100 px/s
  }
  EXPECT_NEAR(k.vel, 100.0, 5.0);
  EXPECT_NEAR(k.pos, 400.0, 3.0);
}

TEST(Tracker, ConfirmsAfterHitsAndKeepsIdentity) {
  Tracker t;
  std::vector<Detection> d;
  for (int f = 0; f < 10; ++f) {
    d = {det_at(100 + f * 4.0, 500), det_at(900 - f * 4.0, 520)};
    t.update(d, 0.04);
  }
  ASSERT_EQ(t.tracks().size(), 2U);
  EXPECT_EQ(t.confirmed_count(), 2U);
  // Identities stay with their trajectories (sorted by x: left mover is id 1).
  const auto& tr = t.tracks();
  const auto left = std::min_element(
      tr.begin(), tr.end(), [](const auto& a, const auto& b) { return a.kx.pos < b.kx.pos; });
  EXPECT_EQ(left->id, 1U);
  EXPECT_GT(left->kx.vel, 50.0);
}

TEST(Tracker, SurvivesShortOcclusionThenDeletes) {
  md::TrackerParams p;
  p.max_misses = 5;
  Tracker t(p);
  for (int f = 0; f < 5; ++f) t.update({det_at(100 + f * 4.0, 500)}, 0.04);
  ASSERT_EQ(t.confirmed_count(), 1U);
  for (int f = 0; f < 5; ++f) t.update({}, 0.04);  // occluded
  EXPECT_EQ(t.tracks().size(), 1U) << "kept during a short gap";
  t.update({det_at(140, 500)}, 0.04);  // reappears near the predicted spot
  ASSERT_EQ(t.tracks().size(), 1U);
  EXPECT_EQ(t.tracks()[0].id, 1U) << "re-associated, not a new track";
  for (int f = 0; f < 7; ++f) t.update({}, 0.04);
  EXPECT_TRUE(t.tracks().empty());
}

TEST(Tracker, SingleFrameNoiseNeverConfirms) {
  Tracker t;
  std::mt19937 rng(1);
  std::uniform_real_distribution<double> d(0, 3000);
  for (int f = 0; f < 50; ++f) {
    t.update({det_at(d(rng), d(rng) / 3)}, 0.04);  // random flicker each frame
  }
  EXPECT_EQ(t.confirmed_count(), 0U);
}

}  // namespace
