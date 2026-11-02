#pragma once

#include "md/director/crop.hpp"
#include "md/track/tracker.hpp"

#include <vector>

namespace md {

// Critically damped spring: moves `value` towards a target as fast as possible
// without overshooting, like a camera operator easing into a pan. Frame-rate
// independent (uses the exact closed-form step), with optional limits on
// velocity so the virtual camera never whips.
struct Spring1D {
  double value{0.0};
  double velocity{0.0};

  // omega = natural frequency (rad/s); higher = snappier.
  void step(double target, double omega, double dt, double max_speed);
};

struct DirectorParams {
  double aspect{16.0 / 9.0};
  // Width of the "where is the play" window, as a fraction of the pitch width.
  double cluster_width_frac{0.40};
  double speed_ref_px_s{120.0};  // speed at which a player's weight doubles
  double max_weight{4.0};
  double lead_seconds{0.35};    // aim ahead of the play's average velocity
  double spread_gain{2.6};      // crop width ~= gain * std-dev of player x
  double margin_px{420.0};      // added to the crop width
  double min_width_px{1100.0};  // tightest zoom (source px)
  double max_width_frac{0.82};  // widest zoom, fraction of the widest legal crop
  double dead_zone_frac{0.06};  // ignore target moves smaller than this fraction of crop width
  double omega_pan{1.6};        // spring stiffness for x/y (rad/s)
  double omega_zoom{0.9};       // zoom eases slower than pan
  double max_pan_px_s{900.0};   // pan speed limit (source px / s)
  double max_zoom_px_s{500.0};
  double vertical_bias{-0.08};  // shift framing up by this fraction of crop height (heads in shot)
};

struct DirectorOutput {
  RectF crop;   // smoothed virtual camera, source pixels
  Vec2 target;  // raw (pre-smoothing) action point
  double target_width{0.0};
  int players{0};  // confirmed tracks considered
};

// Decides where the virtual camera looks.
//
// 1. Action point: among confirmed tracks inside the pitch, find the window of
//    width `cluster_width_frac * pitch width` holding the most "weight"
//    (moving players weigh more). Its weighted centroid, nudged ahead along the
//    group's velocity, is where the play is.
// 2. Zoom: wide enough to cover the group's spread plus a margin.
// 3. Dead zone: small changes of target are ignored (no jitter when play is calm).
// 4. Smoothing: critically damped springs on x, y and width, with speed limits.
class Director {
 public:
  Director(Size source, RectF pitch_bounds_px, DirectorParams params = {});

  const DirectorOutput& update(const std::vector<Track>& tracks, double dt);

  [[nodiscard]] const DirectorOutput& last() const { return out_; }
  [[nodiscard]] const DirectorParams& params() const { return p_; }

 private:
  struct Point {
    double x, y, w;
  };

  Size src_;
  RectF pitch_;
  DirectorParams p_;
  Spring1D sx_, sy_, sw_;
  Vec2 held_target_;
  double held_width_{0.0};
  std::vector<Point> pts_;
  DirectorOutput out_;
};

}  // namespace md
