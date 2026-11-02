#include "md/track/tracker.hpp"

#include <algorithm>
#include <cmath>

namespace md {

void Kalman1D::predict(double dt, double accel_noise) {
  // x' = F x with F = [[1, dt], [0, 1]]
  pos += vel * dt;
  // P' = F P F^T + Q, Q from a white-noise acceleration model.
  const double q = accel_noise * accel_noise;
  const double dt2 = dt * dt;
  const double n00 = p00 + 2.0 * dt * p01 + dt2 * p11 + q * dt2 * dt2 / 4.0;
  const double n01 = p01 + dt * p11 + q * dt2 * dt / 2.0;
  const double n11 = p11 + q * dt2;
  p00 = n00;
  p01 = n01;
  p11 = n11;
}

void Kalman1D::update(double z, double meas_noise) {
  // Measurement H = [1, 0]: we observe position only.
  const double s = p00 + meas_noise * meas_noise;  // innovation variance
  const double k0 = p00 / s;                       // Kalman gain
  const double k1 = p01 / s;
  const double innovation = z - pos;
  pos += k0 * innovation;
  vel += k1 * innovation;
  // P = (I - K H) P
  const double n00 = (1.0 - k0) * p00;
  const double n01 = (1.0 - k0) * p01;
  const double n11 = p11 - k1 * p01;
  p00 = n00;
  p01 = n01;
  p11 = n11;
}

Tracker::Tracker(TrackerParams params) : p_(params) {
  tracks_.reserve(128);
  cost_.reserve(128 * 128);
  det_used_.reserve(256);
}

std::size_t Tracker::confirmed_count() const {
  return static_cast<std::size_t>(
      std::count_if(tracks_.begin(), tracks_.end(), [](const Track& t) { return t.confirmed; }));
}

void Tracker::update(const std::vector<Detection>& dets, double dt) {
  for (Track& t : tracks_) {
    t.kx.predict(dt, p_.accel_noise);
    t.ky.predict(dt, p_.accel_noise);
  }

  const std::size_t nt = tracks_.size();
  const std::size_t nd = dets.size();
  // Pairs beyond the gate get a large finite cost: the solver may still pair
  // them to complete the assignment, and we reject those pairs afterwards.
  const double kFar = p_.gate_px * 10.0;
  cost_.assign(nt * nd, kFar);
  for (std::size_t i = 0; i < nt; ++i) {
    const Vec2 pr = tracks_[i].position();
    for (std::size_t j = 0; j < nd; ++j) {
      const double d = std::hypot(dets[j].foot.x - pr.x, dets[j].foot.y - pr.y);
      if (d <= p_.gate_px) {
        cost_[i * nd + j] = d;
      }
    }
  }
  const std::vector<int>& match = solver_.solve(cost_, nt, nd);

  det_used_.assign(nd, 0);
  for (std::size_t i = 0; i < nt; ++i) {
    Track& t = tracks_[i];
    const int j = match[i];
    if (j >= 0 && cost_[i * nd + static_cast<std::size_t>(j)] <= p_.gate_px) {
      const Detection& d = dets[static_cast<std::size_t>(j)];
      t.kx.update(d.foot.x, p_.meas_noise);
      t.ky.update(d.foot.y, p_.meas_noise);
      t.last_box = d.box;
      ++t.hits;
      t.misses = 0;
      t.confirmed = t.confirmed || t.hits >= p_.confirm_hits;
      det_used_[static_cast<std::size_t>(j)] = 1;
    } else {
      ++t.misses;
    }
  }

  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                               [&](const Track& t) {
                                 // Tentative tracks die on their first miss.
                                 return t.misses > (t.confirmed ? p_.max_misses : 0);
                               }),
                tracks_.end());

  for (std::size_t j = 0; j < nd; ++j) {
    if (det_used_[j] != 0) {
      continue;
    }
    Track t;
    t.id = next_id_++;
    t.kx.pos = dets[j].foot.x;
    t.ky.pos = dets[j].foot.y;
    t.kx.p00 = t.ky.p00 = p_.meas_noise * p_.meas_noise;
    t.kx.p11 = t.ky.p11 = 200.0 * 200.0;  // unknown velocity
    t.last_box = dets[j].box;
    t.hits = 1;
    t.confirmed = p_.confirm_hits <= 1;
    tracks_.push_back(t);
  }
}

}  // namespace md
