#pragma once

#include "md/director/crop.hpp"
#include "md/track/hungarian.hpp"
#include "md/vision/detector.hpp"

#include <cstdint>
#include <vector>

namespace md {

// 1-D constant-velocity Kalman filter (state: position, velocity).
// Two of these (x and y) make a 2-D tracker. Keeping axes independent keeps
// the maths to 2x2 matrices that can be written out by hand and explained.
struct Kalman1D {
  double pos{0.0};
  double vel{0.0};
  // Covariance [[p00, p01], [p01, p11]] (symmetric).
  double p00{100.0};
  double p01{0.0};
  double p11{100.0};

  void predict(double dt, double accel_noise);
  void update(double measured_pos, double meas_noise);
};

struct TrackerParams {
  double gate_px{80.0};      // max distance (source px) between prediction and detection
  double accel_noise{60.0};  // process noise: how hard players can accelerate (px/s^2)
  double meas_noise{6.0};    // detection position noise (px, std-dev)
  int confirm_hits{3};       // hits before a track is reported
  int max_misses{12};        // misses (frames) before a track is deleted
};

struct Track {
  std::uint32_t id{0};
  Kalman1D kx;
  Kalman1D ky;
  RectF last_box;
  int hits{0};
  int misses{0};
  bool confirmed{false};

  [[nodiscard]] Vec2 position() const { return {kx.pos, ky.pos}; }
  [[nodiscard]] Vec2 velocity() const { return {kx.vel, ky.vel}; }
};

// Multi-object tracker: Kalman prediction + Hungarian assignment with gating
// (the core of SORT/ByteTrack, without the appearance model).
class Tracker {
 public:
  explicit Tracker(TrackerParams params = {});

  // Advance by dt seconds and associate this frame's detections.
  void update(const std::vector<Detection>& detections, double dt);

  [[nodiscard]] const std::vector<Track>& tracks() const { return tracks_; }
  [[nodiscard]] std::size_t confirmed_count() const;

 private:
  TrackerParams p_;
  std::vector<Track> tracks_;
  std::vector<double> cost_;
  std::vector<char> det_used_;
  HungarianSolver solver_;
  std::uint32_t next_id_{1};
};

}  // namespace md
