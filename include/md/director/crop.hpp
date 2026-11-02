#pragma once

namespace md {

struct Size {
  int width{0};
  int height{0};
};

struct Vec2 {
  double x{0.0};
  double y{0.0};
};

// Crop rectangle in source-pixel coordinates (sub-pixel precision).
struct RectF {
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};
};

// Crop rectangle snapped to whole, even pixels (YUV 4:2:0 chroma needs even
// offsets and sizes, otherwise the colour planes shift by half a pixel).
struct RectI {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

struct CropLimits {
  double aspect{16.0 / 9.0};  // output width / height
  double min_width{640.0};    // tightest zoom allowed, in source pixels
};

// The "virtual camera": given where the camera wants to look (`center`) and
// how wide it wants to be (`desired_width`, source pixels), return the crop
// rectangle that:
//   1. has exactly `limits.aspect`,
//   2. is no narrower than `limits.min_width` and no larger than the source,
//   3. lies fully inside the source frame (the centre is shifted, never the
//      size, so the zoom level the director asked for is preserved).
[[nodiscard]] RectF compute_crop(Size source, Vec2 center, double desired_width,
                                 const CropLimits& limits = {});

// Snap to even pixels while staying inside the source and keeping the aspect
// ratio as close as integer pixels allow.
[[nodiscard]] RectI snap_to_even(const RectF& crop, Size source);

}  // namespace md
