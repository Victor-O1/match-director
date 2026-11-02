#include "md/director/director.hpp"

#include <algorithm>
#include <cmath>

namespace md {

void Spring1D::step(double target, double omega, double dt, double max_speed) {
  // Exact solution of x'' = -2*omega*x' - omega^2*(x - target) over dt.
  const double e = value - target;
  const double decay = std::exp(-omega * dt);
  const double tmp = (velocity + omega * e) * dt;
  double new_value = target + (e + tmp) * decay;
  double new_velocity = (velocity - omega * tmp) * decay;

  const double max_step = max_speed * dt;
  const double moved = new_value - value;
  if (std::abs(moved) > max_step) {
    new_value = value + std::copysign(max_step, moved);
    new_velocity = std::copysign(max_speed, moved);
  }
  value = new_value;
  velocity = new_velocity;
}

Director::Director(Size source, RectF pitch_bounds_px, DirectorParams params)
    : src_(source), pitch_(pitch_bounds_px), p_(params) {
  pts_.reserve(128);
  // Start on a wide shot of the pitch centre.
  const double max_w = std::min(static_cast<double>(src_.width), src_.height * p_.aspect);
  held_target_ = {pitch_.x + pitch_.width / 2.0, pitch_.y + pitch_.height / 2.0};
  held_width_ = max_w * p_.max_width_frac;
  sx_.value = held_target_.x;
  sy_.value = held_target_.y;
  sw_.value = held_width_;
  out_.crop = compute_crop(src_, held_target_, held_width_, {p_.aspect, p_.min_width_px});
  out_.target = held_target_;
  out_.target_width = held_width_;
}

const DirectorOutput& Director::update(const std::vector<Track>& tracks, double dt) {
  const double max_w =
      std::min(static_cast<double>(src_.width), src_.height * p_.aspect) * p_.max_width_frac;

  pts_.clear();
  for (const Track& t : tracks) {
    if (!t.confirmed || t.misses > 0) {
      continue;
    }
    const Vec2 v = t.velocity();
    const double speed = std::hypot(v.x, v.y);
    const double w = std::min(p_.max_weight, 1.0 + speed / p_.speed_ref_px_s);
    pts_.push_back({t.kx.pos, t.ky.pos, w});
  }
  out_.players = static_cast<int>(pts_.size());

  if (!pts_.empty()) {
    std::sort(pts_.begin(), pts_.end(), [](const Point& a, const Point& b) { return a.x < b.x; });

    // Sliding window over x: the densest group (by weight) is the play.
    const double win = p_.cluster_width_frac * pitch_.width;
    std::size_t best_lo = 0;
    std::size_t best_hi = 1;
    double best_sum = -1.0;
    double sum = 0.0;
    std::size_t lo = 0;
    for (std::size_t hi = 0; hi < pts_.size(); ++hi) {
      sum += pts_[hi].w;
      while (pts_[hi].x - pts_[lo].x > win) {
        sum -= pts_[lo].w;
        ++lo;
      }
      if (sum > best_sum) {
        best_sum = sum;
        best_lo = lo;
        best_hi = hi + 1;
      }
    }

    double wsum = 0.0;
    double mx = 0.0;
    double my = 0.0;
    for (std::size_t i = best_lo; i < best_hi; ++i) {
      wsum += pts_[i].w;
      mx += pts_[i].w * pts_[i].x;
      my += pts_[i].w * pts_[i].y;
    }
    mx /= wsum;
    my /= wsum;
    double var_x = 0.0;
    double var_y = 0.0;
    for (std::size_t i = best_lo; i < best_hi; ++i) {
      var_x += pts_[i].w * (pts_[i].x - mx) * (pts_[i].x - mx);
      var_y += pts_[i].w * (pts_[i].y - my) * (pts_[i].y - my);
    }
    const double sd_x = std::sqrt(var_x / wsum);
    const double sd_y = std::sqrt(var_y / wsum);

    // Group velocity from the tracks inside the window (by position match).
    double gvx = 0.0;
    double gvy = 0.0;
    double gw = 0.0;
    const double x_lo = pts_[best_lo].x;
    const double x_hi = pts_[best_hi - 1].x;
    for (const Track& t : tracks) {
      if (!t.confirmed || t.misses > 0 || t.kx.pos < x_lo || t.kx.pos > x_hi) {
        continue;
      }
      gvx += t.kx.vel;
      gvy += t.ky.vel;
      gw += 1.0;
    }
    if (gw > 0.0) {
      mx += p_.lead_seconds * gvx / gw;
      my += p_.lead_seconds * gvy / gw;
    }

    double want_w = p_.spread_gain * sd_x + p_.margin_px;
    const double want_h = p_.spread_gain * sd_y + p_.margin_px * 0.5;
    want_w = std::max(want_w, want_h * p_.aspect);
    want_w = std::clamp(want_w, std::min(p_.min_width_px, max_w), max_w);
    const double frame_h = want_w / p_.aspect;
    my += p_.vertical_bias * frame_h;

    out_.target = {mx, my};
    out_.target_width = want_w;

    // Dead zone (hysteresis): only move the held target for meaningful changes.
    const double cur_w = sw_.value;
    const double dz_x = p_.dead_zone_frac * cur_w;
    const double dz_y = p_.dead_zone_frac * cur_w / p_.aspect;
    if (std::abs(mx - held_target_.x) > dz_x || std::abs(my - held_target_.y) > dz_y) {
      held_target_ = {mx, my};
    }
    if (std::abs(want_w - held_width_) > 2.0 * p_.dead_zone_frac * cur_w) {
      held_width_ = want_w;
    }
  }

  sx_.step(held_target_.x, p_.omega_pan, dt, p_.max_pan_px_s);
  sy_.step(held_target_.y, p_.omega_pan, dt, p_.max_pan_px_s * 0.5);
  sw_.step(held_width_, p_.omega_zoom, dt, p_.max_zoom_px_s);

  out_.crop = compute_crop(src_, {sx_.value, sy_.value}, sw_.value, {p_.aspect, p_.min_width_px});

  // Anti-windup: if the crop had to be pushed inside the frame, pull the
  // spring state onto the legal value so it doesn't keep pushing outward.
  const double cx = out_.crop.x + out_.crop.width / 2.0;
  const double cy = out_.crop.y + out_.crop.height / 2.0;
  if (std::abs(cx - sx_.value) > 1e-6) {
    sx_.value = cx;
    sx_.velocity = 0.0;
  }
  if (std::abs(cy - sy_.value) > 1e-6) {
    sy_.value = cy;
    sy_.velocity = 0.0;
  }
  if (std::abs(out_.crop.width - sw_.value) > 1e-6) {
    sw_.value = out_.crop.width;
    sw_.velocity = 0.0;
  }
  return out_;
}

}  // namespace md
