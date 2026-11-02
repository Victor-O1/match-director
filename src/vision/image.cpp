#include "md/vision/image.hpp"

#include <cassert>

namespace md {

void downscale_box(GrayView src, int factor, GrayImage& dst) {
  assert(factor >= 1);
  assert(dst.width == src.width / factor && dst.height == src.height / factor);
  const auto stride = static_cast<std::size_t>(src.stride);
  const auto f = static_cast<std::size_t>(factor);
  const auto area = static_cast<unsigned>(factor * factor);

  if (factor == 2) {
    // Hot path at the default setting: a tight loop the compiler vectorises.
    for (int y = 0; y < dst.height; ++y) {
      const std::uint8_t* r0 = src.data + static_cast<std::size_t>(2 * y) * stride;
      const std::uint8_t* r1 = r0 + stride;
      std::uint8_t* out = dst.row(y);
      for (int x = 0; x < dst.width; ++x) {
        const auto i = static_cast<std::size_t>(2 * x);
        const unsigned sum = static_cast<unsigned>(r0[i]) + r0[i + 1] + r1[i] + r1[i + 1];
        out[x] = static_cast<std::uint8_t>((sum + 2) >> 2);
      }
    }
    return;
  }

  for (int y = 0; y < dst.height; ++y) {
    std::uint8_t* out = dst.row(y);
    for (int x = 0; x < dst.width; ++x) {
      unsigned sum = 0;
      for (std::size_t dy = 0; dy < f; ++dy) {
        const std::uint8_t* r = src.data + (static_cast<std::size_t>(y) * f + dy) * stride +
                                static_cast<std::size_t>(x) * f;
        for (std::size_t dx = 0; dx < f; ++dx) {
          sum += r[dx];
        }
      }
      out[x] = static_cast<std::uint8_t>((sum + area / 2) / area);
    }
  }
}

}  // namespace md
