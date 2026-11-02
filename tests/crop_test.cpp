#include "md/director/crop.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

using md::compute_crop;
using md::RectF;
using md::Size;
using md::snap_to_even;
using md::Vec2;

constexpr Size k4kPano{3840, 1080};  // wide panoramic strip
constexpr double kAspect = 16.0 / 9.0;

void expect_inside(const RectF& r, Size s) {
  EXPECT_GE(r.x, 0.0);
  EXPECT_GE(r.y, 0.0);
  EXPECT_LE(r.x + r.width, s.width + 1e-9);
  EXPECT_LE(r.y + r.height, s.height + 1e-9);
}

TEST(Crop, CentredRequestIsHonoured) {
  const RectF r = compute_crop(k4kPano, {1920, 540}, 1280);
  EXPECT_DOUBLE_EQ(r.width, 1280);
  EXPECT_DOUBLE_EQ(r.height, 720);
  EXPECT_DOUBLE_EQ(r.x, 1280);
  EXPECT_DOUBLE_EQ(r.y, 180);
}

TEST(Crop, AspectIsAlwaysExact) {
  for (double w : {10.0, 700.0, 1500.0, 99999.0}) {
    const RectF r = compute_crop(k4kPano, {500, 500}, w);
    EXPECT_NEAR(r.width / r.height, kAspect, 1e-12) << "requested width " << w;
  }
}

TEST(Crop, TooWideIsLimitedBySourceHeight) {
  // 1080 rows high -> widest 16:9 crop is 1920 wide.
  const RectF r = compute_crop(k4kPano, {1920, 540}, 5000);
  EXPECT_DOUBLE_EQ(r.width, 1920);
  EXPECT_DOUBLE_EQ(r.height, 1080);
}

TEST(Crop, TooNarrowIsLimitedByMinWidth) {
  const RectF r = compute_crop(k4kPano, {1920, 540}, 10, {kAspect, 640});
  EXPECT_DOUBLE_EQ(r.width, 640);
}

TEST(Crop, CentreNearEdgeShiftsCropButKeepsZoom) {
  const RectF left = compute_crop(k4kPano, {0, 0}, 1280);
  EXPECT_DOUBLE_EQ(left.x, 0);
  EXPECT_DOUBLE_EQ(left.y, 0);
  EXPECT_DOUBLE_EQ(left.width, 1280);

  const RectF right = compute_crop(k4kPano, {3840, 1080}, 1280);
  EXPECT_DOUBLE_EQ(right.x, 3840 - 1280);
  EXPECT_DOUBLE_EQ(right.y, 1080 - 720);
  expect_inside(right, k4kPano);
}

TEST(Crop, NanInputFallsBackToWideCentredShot) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const RectF r = compute_crop(k4kPano, {nan, 100}, nan);
  EXPECT_DOUBLE_EQ(r.width, 1920);
  EXPECT_DOUBLE_EQ(r.x, 960);
  expect_inside(r, k4kPano);
}

TEST(Crop, SourceSmallerThanMinWidth) {
  constexpr Size tiny{320, 180};
  const RectF r = compute_crop(tiny, {160, 90}, 1000);
  EXPECT_DOUBLE_EQ(r.width, 320);
  EXPECT_DOUBLE_EQ(r.height, 180);
}

TEST(SnapToEven, ProducesEvenValuesInsideSource) {
  const RectF r = compute_crop(k4kPano, {1001.3, 377.7}, 1279.4);
  const auto s = snap_to_even(r, k4kPano);
  EXPECT_EQ(s.x % 2, 0);
  EXPECT_EQ(s.y % 2, 0);
  EXPECT_EQ(s.width % 2, 0);
  EXPECT_EQ(s.height % 2, 0);
  EXPECT_LE(s.x + s.width, k4kPano.width);
  EXPECT_LE(s.y + s.height, k4kPano.height);
}

TEST(SnapToEven, OddSourceSizeStaysInside) {
  constexpr Size odd{1921, 1081};
  const RectF r = compute_crop(odd, {1921, 1081}, 99999);
  const auto s = snap_to_even(r, odd);
  EXPECT_LE(s.x + s.width, odd.width);
  EXPECT_LE(s.y + s.height, odd.height);
  EXPECT_EQ(s.width % 2, 0);
}

}  // namespace
