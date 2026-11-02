#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace md {

struct Glyph {
  float u0{0}, v0{0}, u1{0}, v1{0};  // atlas texture coordinates
  int width{0};
  int height{0};
  int bearing_x{0};  // pen position -> left edge of bitmap
  int bearing_y{0};  // baseline -> top edge of bitmap
  int advance{0};    // pen advance in pixels
};

// Rasterises printable ASCII at several pixel sizes into ONE 8-bit atlas
// bitmap with FreeType. All text on screen then becomes textured quads that
// sample this single texture, so every caption, score and clock on a frame is
// drawn in one GL draw call (no per-glyph texture binds or uploads).
//
// CPU-only: the GL upload of `pixels()` is done by the compositor.
class GlyphAtlas {
 public:
  static constexpr int kFirst = 32;
  static constexpr int kLast = 126;

  GlyphAtlas(const std::string& font_path, const std::vector<int>& pixel_sizes,
             int atlas_width = 1024);

  [[nodiscard]] const Glyph& glyph(std::size_t font, char c) const;
  [[nodiscard]] int line_ascent(std::size_t font) const { return fonts_[font].ascent; }
  [[nodiscard]] int line_height(std::size_t font) const { return fonts_[font].height; }
  [[nodiscard]] int text_width(std::size_t font, std::string_view text) const;
  [[nodiscard]] std::size_t font_count() const { return fonts_.size(); }

  [[nodiscard]] const std::vector<std::uint8_t>& pixels() const { return pixels_; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }

 private:
  struct Font {
    std::array<Glyph, kLast - kFirst + 1> glyphs{};
    int ascent{0};
    int height{0};
  };
  std::vector<Font> fonts_;
  std::vector<std::uint8_t> pixels_;
  int width_{0};
  int height_{0};
};

}  // namespace md
