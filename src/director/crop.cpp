#include "md/director/crop.hpp"

#include <algorithm>
#include <cmath>

namespace md {

RectF compute_crop(Size source, Vec2 center, double desired_width, const CropLimits& limits) {
  const auto src_w = static_cast<double>(source.width);
  const auto src_h = static_cast<double>(source.height);

  // Widest crop of the requested aspect that still fits in the source.
  const double max_width = std::min(src_w, src_h * limits.aspect);
  const double min_width = std::min(limits.min_width, max_width);

  // NaN (e.g. from a diverged tracker) would poison every later frame;
  // fall back to the widest shot centred on the frame.
  if (!std::isfinite(desired_width)) {
    desired_width = max_width;
  }
  if (!std::isfinite(center.x) || !std::isfinite(center.y)) {
    center = {src_w / 2.0, src_h / 2.0};
  }

  RectF r;
  r.width = std::clamp(desired_width, min_width, max_width);
  r.height = r.width / limits.aspect;
  r.x = std::clamp(center.x - r.width / 2.0, 0.0, src_w - r.width);
  r.y = std::clamp(center.y - r.height / 2.0, 0.0, src_h - r.height);
  return r;
}

namespace {

int round_down_even(double v) { return static_cast<int>(std::floor(v / 2.0)) * 2; }
int round_to_even(double v) { return static_cast<int>(std::lround(v / 2.0)) * 2; }

}  // namespace

RectI snap_to_even(const RectF& crop, Size source) {
  const int max_w = round_down_even(source.width);
  const int max_h = round_down_even(source.height);

  RectI r;
  r.width = std::clamp(round_to_even(crop.width), 2, max_w);
  r.height = std::clamp(round_to_even(crop.height), 2, max_h);
  r.x = std::clamp(round_to_even(crop.x), 0, max_w - r.width);
  r.y = std::clamp(round_to_even(crop.y), 0, max_h - r.height);
  return r;
}

}  // namespace md
