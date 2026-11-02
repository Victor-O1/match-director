#pragma once

#include "md/control/command.hpp"
#include "md/gfx/glyph_atlas.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace md {

// One vertex of the overlay layer. Positions are output pixels (top-left origin).
struct OverlayVertex {
  float x, y;
  float u, v;
  std::uint32_t rgba;  // R in the low byte (matches GL_UNSIGNED_BYTE x4 on little-endian)
  float mode;          // 0 = solid colour, 1 = text (alpha from the glyph atlas)
};
static_assert(sizeof(OverlayVertex) == 24);

[[nodiscard]] constexpr std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                           std::uint8_t a = 255) {
  return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
         (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
}

// Collects every overlay quad for one frame into a single vertex array, so the
// whole graphics layer is uploaded once and drawn with one draw call.
class OverlayBatch {
 public:
  OverlayBatch() { verts_.reserve(6 * 512); }

  void clear() { verts_.clear(); }
  void rect(float x, float y, float w, float h, std::uint32_t color);
  // Draws `text` with its baseline at y. Returns the advance width in pixels.
  float text(const GlyphAtlas& atlas, std::size_t font, float x, float baseline,
             std::string_view text, std::uint32_t color);

  [[nodiscard]] const std::vector<OverlayVertex>& vertices() const { return verts_; }

 private:
  void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
            std::uint32_t color, float mode);
  std::vector<OverlayVertex> verts_;
};

// Font slots the compositor creates in the atlas (sizes scale with output height).
enum FontSlot : std::size_t { kFontScore = 0, kFontCaption = 1, kFontSmall = 2 };

struct OverlayContext {
  int width{1280};
  int height{720};
  double out_time{0.0};     // seconds since start of output
  double source_time{0.0};  // seconds into the source video
  bool replay{false};
  double replay_progress{0.0};  // 0..1 through the current replay
};

// Lays out the broadcast graphics (scoreboard + clock, lower third with a
// slide-in animation, LIVE / REPLAY bug) for one frame.
void build_overlay(OverlayBatch& batch, const GlyphAtlas& atlas, const ScoreboardState& state,
                   const OverlayContext& ctx);

// "MM:SS" for a match clock (negative values clamp to 00:00).
[[nodiscard]] std::string format_clock(double seconds);

}  // namespace md
