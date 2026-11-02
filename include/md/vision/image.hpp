#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace md {

// Non-owning view of an 8-bit single-channel image (e.g. a luma plane).
struct GrayView {
  const std::uint8_t* data{nullptr};
  int width{0};
  int height{0};
  int stride{0};  // bytes per row, >= width

  [[nodiscard]] std::uint8_t at(int x, int y) const {
    return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(x)];
  }
};

// Owning, tightly packed 8-bit image. Allocated once and reused.
struct GrayImage {
  std::vector<std::uint8_t> pixels;
  int width{0};
  int height{0};

  GrayImage() = default;
  GrayImage(int w, int h)
      : pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h)), width(w), height(h) {}

  [[nodiscard]] GrayView view() const { return {pixels.data(), width, height, width}; }
  [[nodiscard]] std::uint8_t* row(int y) {
    return pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
  }
  [[nodiscard]] const std::uint8_t* row(int y) const {
    return pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
  }
};

// Box-filter downscale by an integer factor (factor x factor pixels averaged).
// `dst` must already be (src.width / factor) x (src.height / factor).
void downscale_box(GrayView src, int factor, GrayImage& dst);

}  // namespace md
