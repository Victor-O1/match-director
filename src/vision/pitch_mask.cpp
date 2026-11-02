#include "md/vision/pitch_mask.hpp"

#include <algorithm>

namespace md {

bool point_in_polygon(const std::vector<Vec2>& poly, Vec2 p) {
  bool inside = false;
  const std::size_t n = poly.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const Vec2 a = poly[i];
    const Vec2 b = poly[j];
    // Does the horizontal ray from p to +x cross edge (a, b)?
    if ((a.y > p.y) != (b.y > p.y)) {
      const double x_cross = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
      if (p.x < x_cross) {
        inside = !inside;
      }
    }
  }
  return inside;
}

PitchMask::PitchMask(const std::vector<Vec2>& polygon, int width, int height)
    : mask_(width, height) {
  if (polygon.size() < 3) {
    std::fill(mask_.pixels.begin(), mask_.pixels.end(), std::uint8_t{255});
    return;
  }
  double min_x = 1.0;
  double min_y = 1.0;
  double max_x = 0.0;
  double max_y = 0.0;
  for (const Vec2& v : polygon) {
    min_x = std::min(min_x, v.x);
    min_y = std::min(min_y, v.y);
    max_x = std::max(max_x, v.x);
    max_y = std::max(max_y, v.y);
  }
  bounds_ = {std::clamp(min_x, 0.0, 1.0), std::clamp(min_y, 0.0, 1.0),
             std::clamp(max_x, 0.0, 1.0) - std::clamp(min_x, 0.0, 1.0),
             std::clamp(max_y, 0.0, 1.0) - std::clamp(min_y, 0.0, 1.0)};

  // Runs once at start-up, so the simple per-pixel test is fine.
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = mask_.row(y);
    const double ny = (y + 0.5) / height;
    for (int x = 0; x < width; ++x) {
      const double nx = (x + 0.5) / width;
      row[x] = point_in_polygon(polygon, {nx, ny}) ? 255 : 0;
    }
  }
}

}  // namespace md
