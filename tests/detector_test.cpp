#include "md/vision/detector.hpp"
#include "md/vision/image.hpp"
#include "md/vision/pitch_mask.hpp"

#include <gtest/gtest.h>

#include <random>

namespace {

using md::BackgroundDetector;
using md::GrayImage;

constexpr int kW = 640;
constexpr int kH = 360;

// Flat grass with a little per-frame noise.
GrayImage background(std::mt19937& rng, int noise) {
  GrayImage img(kW, kH);
  std::uniform_int_distribution<int> n(-noise, noise);
  for (auto& p : img.pixels) p = static_cast<std::uint8_t>(80 + n(rng));
  return img;
}

void draw_player(GrayImage& img, int x, int y, int w, int h, std::uint8_t v) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx) img.row(yy)[xx] = v;
}

md::DetectorParams params() {
  md::DetectorParams p;
  p.downscale = 2;
  p.warmup_frames = 10;
  p.min_area = 6;
  p.min_height = 5;
  return p;
}

TEST(Downscale, AveragesBlocks) {
  GrayImage src(4, 2);
  src.pixels = {0, 4, 8, 8, 0, 4, 8, 8};
  GrayImage dst(2, 1);
  md::downscale_box(src.view(), 2, dst);
  EXPECT_EQ(dst.pixels[0], 2);
  EXPECT_EQ(dst.pixels[1], 8);
}

TEST(PitchMask, PolygonInsideOutside) {
  const std::vector<md::Vec2> square = {{0.25, 0.25}, {0.75, 0.25}, {0.75, 0.75}, {0.25, 0.75}};
  md::PitchMask m(square, 100, 100);
  EXPECT_TRUE(m.inside(50, 50));
  EXPECT_FALSE(m.inside(5, 5));
  EXPECT_FALSE(m.inside(90, 50));
}

TEST(BackgroundDetector, FindsMovingPlayerOnly) {
  std::mt19937 rng(7);
  BackgroundDetector det(kW, kH, md::PitchMask({}, kW / 2, kH / 2), params());
  std::vector<md::Detection> last;
  for (int f = 0; f < 60; ++f) {
    GrayImage img = background(rng, 3);
    if (f >= 30) draw_player(img, 100 + (f - 30) * 3, 150, 10, 30, 220);  // walks right
    last = det.detect(img.view());
  }
  ASSERT_EQ(last.size(), 1U);
  EXPECT_NEAR(last[0].foot.y, 180, 4);
  EXPECT_NEAR(last[0].foot.x, 100 + 29 * 3 + 5, 6);
}

TEST(BackgroundDetector, IgnoresSingleFrameFlicker) {
  std::mt19937 rng(9);
  BackgroundDetector det(kW, kH, md::PitchMask({}, kW / 2, kH / 2), params());
  for (int f = 0; f < 40; ++f) {
    GrayImage img = background(rng, 3);
    if (f % 2 == 1) draw_player(img, 300, 100, 10, 30, 220);  // on/off every frame
    const auto& d = det.detect(img.view());
    if (f > 12) {
      EXPECT_TRUE(d.empty()) << "frame " << f;
    }
  }
}

TEST(BackgroundDetector, RejectsFlatShadowShapes) {
  std::mt19937 rng(11);
  BackgroundDetector det(kW, kH, md::PitchMask({}, kW / 2, kH / 2), params());
  std::vector<md::Detection> last;
  for (int f = 0; f < 40; ++f) {
    GrayImage img = background(rng, 3);
    if (f >= 20) draw_player(img, 100 + f * 2, 200, 60, 6, 30);  // long flat dark streak
    last = det.detect(img.view());
  }
  EXPECT_TRUE(last.empty());
}

TEST(BackgroundDetector, NoisyPixelsAdaptTheirThreshold) {
  // Region that flickers heavily every frame (like wet turf under floodlights).
  std::mt19937 rng(13);
  std::uniform_int_distribution<int> big(-30, 30);
  BackgroundDetector det(kW, kH, md::PitchMask({}, kW / 2, kH / 2), params());
  std::size_t late = 0;
  for (int f = 0; f < 300; ++f) {
    GrayImage img = background(rng, 2);
    for (int y = 50; y < 120; ++y)
      for (int x = 400; x < 500; ++x) img.row(y)[x] = static_cast<std::uint8_t>(110 + big(rng));
    const auto& d = det.detect(img.view());
    if (f > 250) late += d.size();
  }
  EXPECT_EQ(late, 0U) << "after learning, the noisy patch stops producing detections";
}

}  // namespace
