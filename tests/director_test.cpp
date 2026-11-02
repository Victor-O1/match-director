#include "md/director/director.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

using md::Director;
using md::Spring1D;
using md::Track;

constexpr md::Size kSrc{3840, 1906};
const md::RectF kPitch{380, 530, 3100, 940};

Track track_at(std::uint32_t id, double x, double y, double vx = 0) {
  Track t;
  t.id = id;
  t.kx.pos = x;
  t.ky.pos = y;
  t.kx.vel = vx;
  t.confirmed = true;
  return t;
}

TEST(Spring, ReachesTargetWithoutOvershoot) {
  Spring1D s;
  s.value = 0;
  double max_v = 0;
  for (int i = 0; i < 500; ++i) {
    s.step(100.0, 2.0, 0.04, 1e9);
    max_v = std::max(max_v, s.value);
  }
  EXPECT_NEAR(s.value, 100.0, 0.01);
  EXPECT_LE(max_v, 100.0 + 1e-9) << "critically damped: no overshoot";
}

TEST(Spring, FrameRateIndependent) {
  Spring1D a;
  Spring1D b;
  for (int i = 0; i < 25; ++i) a.step(100, 2.0, 0.04, 1e9);  // 1 s at 25 fps
  for (int i = 0; i < 50; ++i) b.step(100, 2.0, 0.02, 1e9);  // 1 s at 50 fps
  EXPECT_NEAR(a.value, b.value, 1e-9);
}

TEST(Spring, RespectsSpeedLimit) {
  Spring1D s;
  s.step(10000, 50.0, 0.04, 500.0);
  EXPECT_LE(std::abs(s.value), 500.0 * 0.04 + 1e-9);
}

TEST(Director, FollowsTheGroupAndStaysInFrame) {
  Director d(kSrc, kPitch);
  std::vector<Track> tr;
  for (std::uint32_t i = 0; i < 8; ++i) tr.push_back(track_at(i + 1, 2800 + i * 40.0, 900));
  tr.push_back(track_at(99, 600, 900));  // lone goalkeeper far away
  md::DirectorOutput out;
  for (int f = 0; f < 250; ++f) out = d.update(tr, 0.04);  // 10 s
  const double cx = out.crop.x + out.crop.width / 2;
  EXPECT_NEAR(cx, 2940, 150) << "camera settles on the group, not the goalkeeper";
  EXPECT_GE(out.crop.x, 0);
  EXPECT_LE(out.crop.x + out.crop.width, kSrc.width + 1e-6);
  EXPECT_NEAR(out.crop.width / out.crop.height, 16.0 / 9.0, 1e-9);
}

TEST(Director, DeadZoneHoldsCameraForSmallMoves) {
  Director d(kSrc, kPitch);
  std::vector<Track> tr = {track_at(1, 1900, 900), track_at(2, 2000, 900)};
  for (int f = 0; f < 250; ++f) d.update(tr, 0.04);
  const double settled = d.last().crop.x;
  tr[0].kx.pos += 15;  // tiny shuffle
  for (int f = 0; f < 50; ++f) d.update(tr, 0.04);
  EXPECT_NEAR(d.last().crop.x, settled, 1.0);
}

TEST(Director, NoTracksKeepsLastShot) {
  Director d(kSrc, kPitch);
  // Let the camera finish its move first: "hold" means the target is kept,
  // not that an in-progress pan freezes mid-way.
  for (int f = 0; f < 400; ++f) d.update({track_at(1, 3000, 900), track_at(2, 3050, 900)}, 0.04);
  const md::RectF before = d.last().crop;
  for (int f = 0; f < 100; ++f) d.update({}, 0.04);
  EXPECT_NEAR(d.last().crop.x, before.x, 0.5);
}

TEST(Director, PanSpeedIsLimited) {
  md::DirectorParams p;
  p.max_pan_px_s = 600;
  Director d(kSrc, kPitch, p);
  for (int f = 0; f < 100; ++f) d.update({track_at(1, 500, 900), track_at(2, 550, 900)}, 0.04);
  // The limit applies to the camera CENTRE (crop.x also moves when zoom changes).
  auto centre = [&] { return d.last().crop.x + d.last().crop.width / 2.0; };
  double prev = centre();
  for (int f = 0; f < 50; ++f) {
    d.update({track_at(1, 3300, 900), track_at(2, 3350, 900)}, 0.04);  // play switches wings
    EXPECT_LE(std::abs(centre() - prev), 600 * 0.04 + 1e-6);
    prev = centre();
  }
}

}  // namespace
