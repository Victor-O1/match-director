#pragma once

#include "md/director/crop.hpp"
#include "md/vision/image.hpp"

#include <vector>

namespace md {

// Region of the frame where players can be: the pitch plus a small margin.
// Defined as a polygon in normalised [0,1] source coordinates so the same
// config works at any analysis resolution. Everything outside (stands,
// benches, spectators, floodlights) is ignored by detection.
class PitchMask {
 public:
  PitchMask() = default;
  // Rasterises the polygon at width x height. An empty polygon means "whole frame".
  PitchMask(const std::vector<Vec2>& polygon_normalised, int width, int height);

  [[nodiscard]] bool inside(int x, int y) const {
    return mask_.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask_.width) +
                        static_cast<std::size_t>(x)] != 0;
  }
  [[nodiscard]] const GrayImage& image() const { return mask_; }
  // Bounding box of the mask in normalised coordinates.
  [[nodiscard]] RectF bounds_normalised() const { return bounds_; }

 private:
  GrayImage mask_;
  RectF bounds_{0, 0, 1, 1};
};

// Even-odd point-in-polygon test.
[[nodiscard]] bool point_in_polygon(const std::vector<Vec2>& poly, Vec2 p);

}  // namespace md
