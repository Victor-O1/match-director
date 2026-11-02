#include "md/gfx/glyph_atlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace md {

namespace {

struct FtLibraryDone {
  void operator()(FT_Library lib) const noexcept { FT_Done_FreeType(lib); }
};
struct FtFaceDone {
  void operator()(FT_Face face) const noexcept { FT_Done_Face(face); }
};

}  // namespace

GlyphAtlas::GlyphAtlas(const std::string& font_path, const std::vector<int>& pixel_sizes,
                       int atlas_width)
    : width_(atlas_width) {
  FT_Library raw_lib = nullptr;
  if (FT_Init_FreeType(&raw_lib) != 0) {
    throw std::runtime_error("FreeType init failed");
  }
  std::unique_ptr<FT_LibraryRec_, FtLibraryDone> lib(raw_lib);
  FT_Face raw_face = nullptr;
  if (FT_New_Face(lib.get(), font_path.c_str(), 0, &raw_face) != 0) {
    throw std::runtime_error("cannot load font: " + font_path);
  }
  std::unique_ptr<FT_FaceRec_, FtFaceDone> face(raw_face);

  // Pass 1: render every glyph into its own bitmap and shelf-pack positions.
  struct Pending {
    std::vector<std::uint8_t> bitmap;
    int w, h, x, y;
  };
  std::vector<std::vector<Pending>> pending(pixel_sizes.size());
  fonts_.resize(pixel_sizes.size());
  int pen_x = 1;
  int pen_y = 1;
  int shelf_h = 0;
  for (std::size_t f = 0; f < pixel_sizes.size(); ++f) {
    FT_Set_Pixel_Sizes(face.get(), 0, static_cast<FT_UInt>(pixel_sizes[f]));
    fonts_[f].ascent = static_cast<int>(face->size->metrics.ascender >> 6);
    fonts_[f].height = static_cast<int>(face->size->metrics.height >> 6);
    for (int c = kFirst; c <= kLast; ++c) {
      if (FT_Load_Char(face.get(), static_cast<FT_ULong>(c), FT_LOAD_RENDER) != 0) {
        throw std::runtime_error("FreeType: cannot render glyph");
      }
      const FT_GlyphSlot g = face->glyph;
      const int w = static_cast<int>(g->bitmap.width);
      const int h = static_cast<int>(g->bitmap.rows);
      if (pen_x + w + 1 > width_) {
        pen_x = 1;
        pen_y += shelf_h + 1;
        shelf_h = 0;
      }
      Pending p{std::vector<std::uint8_t>(static_cast<std::size_t>(w * h)), w, h, pen_x, pen_y};
      for (int row = 0; row < h; ++row) {
        std::memcpy(p.bitmap.data() + static_cast<std::size_t>(row * w),
                    g->bitmap.buffer + static_cast<std::ptrdiff_t>(row) * g->bitmap.pitch,
                    static_cast<std::size_t>(w));
      }
      Glyph& gl = fonts_[f].glyphs[static_cast<std::size_t>(c - kFirst)];
      gl.width = w;
      gl.height = h;
      gl.bearing_x = g->bitmap_left;
      gl.bearing_y = g->bitmap_top;
      gl.advance = static_cast<int>(g->advance.x >> 6);
      pending[f].push_back(std::move(p));
      pen_x += w + 1;  // 1 px gutter so bilinear filtering never bleeds neighbours
      shelf_h = std::max(shelf_h, h);
    }
  }
  height_ = pen_y + shelf_h + 1;

  // Pass 2: blit into the atlas and compute texture coordinates.
  pixels_.assign(static_cast<std::size_t>(width_ * height_), 0);
  for (std::size_t f = 0; f < pending.size(); ++f) {
    for (std::size_t i = 0; i < pending[f].size(); ++i) {
      const Pending& p = pending[f][i];
      for (int row = 0; row < p.h; ++row) {
        std::memcpy(pixels_.data() + static_cast<std::size_t>((p.y + row) * width_ + p.x),
                    p.bitmap.data() + static_cast<std::size_t>(row * p.w),
                    static_cast<std::size_t>(p.w));
      }
      Glyph& g = fonts_[f].glyphs[i];
      g.u0 = static_cast<float>(p.x) / static_cast<float>(width_);
      g.v0 = static_cast<float>(p.y) / static_cast<float>(height_);
      g.u1 = static_cast<float>(p.x + p.w) / static_cast<float>(width_);
      g.v1 = static_cast<float>(p.y + p.h) / static_cast<float>(height_);
    }
  }
}

const Glyph& GlyphAtlas::glyph(std::size_t font, char c) const {
  int code = static_cast<unsigned char>(c);
  if (code < kFirst || code > kLast) {
    code = '?';
  }
  return fonts_[font].glyphs[static_cast<std::size_t>(code - kFirst)];
}

int GlyphAtlas::text_width(std::size_t font, std::string_view text) const {
  int w = 0;
  for (char c : text) {
    w += glyph(font, c).advance;
  }
  return w;
}

}  // namespace md
