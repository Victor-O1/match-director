#include "md/gfx/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace md {

void OverlayBatch::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                        float v1, std::uint32_t color, float mode) {
  // Two triangles, no index buffer: 6 vertices per quad.
  verts_.push_back({x0, y0, u0, v0, color, mode});
  verts_.push_back({x1, y0, u1, v0, color, mode});
  verts_.push_back({x0, y1, u0, v1, color, mode});
  verts_.push_back({x1, y0, u1, v0, color, mode});
  verts_.push_back({x1, y1, u1, v1, color, mode});
  verts_.push_back({x0, y1, u0, v1, color, mode});
}

void OverlayBatch::rect(float x, float y, float w, float h, std::uint32_t color) {
  quad(x, y, x + w, y + h, 0.f, 0.f, 0.f, 0.f, color, 0.f);
}

float OverlayBatch::text(const GlyphAtlas& atlas, std::size_t font, float x, float baseline,
                         std::string_view s, std::uint32_t color) {
  float pen = std::round(x);
  const float base = std::round(baseline);
  for (char c : s) {
    const Glyph& g = atlas.glyph(font, c);
    if (g.width > 0 && g.height > 0) {
      const float gx = pen + static_cast<float>(g.bearing_x);
      const float gy = base - static_cast<float>(g.bearing_y);
      quad(gx, gy, gx + static_cast<float>(g.width), gy + static_cast<float>(g.height), g.u0, g.v0,
           g.u1, g.v1, color, 1.f);
    }
    pen += static_cast<float>(g.advance);
  }
  return pen - std::round(x);
}

std::string format_clock(double seconds) {
  // Clamp to 99:59 so the field width is fixed and the buffer provably fits.
  const long total = std::clamp(seconds > 0.0 ? static_cast<long>(seconds) : 0L, 0L, 99L * 60 + 59);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", static_cast<int>(total / 60),
                static_cast<int>(total % 60));
  return buf;
}

namespace {

constexpr std::uint32_t kPanel = rgba(16, 24, 40, 235);
constexpr std::uint32_t kPanelLight = rgba(240, 242, 245, 255);
constexpr std::uint32_t kClockBg = rgba(36, 44, 60, 235);
constexpr std::uint32_t kWhite = rgba(255, 255, 255);
constexpr std::uint32_t kInk = rgba(16, 24, 40);
constexpr std::uint32_t kHomeColor = rgba(37, 99, 235);    // blue kit
constexpr std::uint32_t kAwayColor = rgba(229, 231, 235);  // white kit
constexpr std::uint32_t kAccent = rgba(234, 179, 8);
constexpr std::uint32_t kLive = rgba(220, 38, 38);

std::uint32_t with_alpha(std::uint32_t c, double a) {
  const auto base = static_cast<double>(c >> 24);
  const auto alpha = static_cast<std::uint32_t>(std::clamp(base * a, 0.0, 255.0));
  return (c & 0x00FFFFFFu) | (alpha << 24);
}

double ease_out_cubic(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
}

}  // namespace

void build_overlay(OverlayBatch& b, const GlyphAtlas& atlas, const ScoreboardState& st,
                   const OverlayContext& ctx) {
  b.clear();
  const float s = static_cast<float>(ctx.height) / 720.f;  // layout designed at 720p
  const float pad = 12.f * s;

  // ---- Scoreboard: [HOME][ 1 - 0 ][AWAY][ 12:34 ] -------------------------
  {
    const float x0 = 36.f * s;
    const float y0 = 28.f * s;
    const float h = 40.f * s;
    const int asc = atlas.line_ascent(kFontScore);
    const float baseline = y0 + (h + static_cast<float>(asc) * 0.72f) / 2.f;

    const std::string score = std::to_string(st.home_score) + " - " + std::to_string(st.away_score);
    const std::string clock = format_clock(ctx.source_time - st.kickoff_source_time);
    const float w_home = static_cast<float>(atlas.text_width(kFontScore, st.home)) + 2.f * pad;
    const float w_away = static_cast<float>(atlas.text_width(kFontScore, st.away)) + 2.f * pad;
    const float w_score = static_cast<float>(atlas.text_width(kFontScore, score)) + 2.f * pad;
    const float w_clock = static_cast<float>(atlas.text_width(kFontScore, "00:00")) + 2.f * pad;
    const float stripe = 4.f * s;

    float x = x0;
    b.rect(x, y0, w_home, h, kPanel);
    b.rect(x, y0 + h - stripe, w_home, stripe, kHomeColor);
    b.text(atlas, kFontScore, x + pad, baseline, st.home, kWhite);
    x += w_home;
    b.rect(x, y0, w_score, h, kPanelLight);
    b.text(atlas, kFontScore, x + pad, baseline, score, kInk);
    x += w_score;
    b.rect(x, y0, w_away, h, kPanel);
    b.rect(x, y0 + h - stripe, w_away, stripe, kAwayColor);
    b.text(atlas, kFontScore, x + pad, baseline, st.away, kWhite);
    x += w_away;
    b.rect(x, y0, w_clock, h, kClockBg);
    b.text(atlas, kFontScore, x + pad, baseline, clock, kWhite);
  }

  // ---- LIVE / REPLAY bug (top right) ---------------------------------------
  {
    const std::string_view label = ctx.replay ? "REPLAY" : "LIVE";
    const float h = 32.f * s;
    const float w = static_cast<float>(atlas.text_width(kFontSmall, label)) + 2.f * pad + 14.f * s;
    const float x = static_cast<float>(ctx.width) - 36.f * s - w;
    const float y = 32.f * s;
    const int asc = atlas.line_ascent(kFontSmall);
    const float baseline = y + (h + static_cast<float>(asc) * 0.72f) / 2.f;
    b.rect(x, y, w, h, ctx.replay ? kAccent : kPanel);
    b.rect(x + pad * 0.6f, y + h / 2.f - 4.f * s, 8.f * s, 8.f * s, ctx.replay ? kInk : kLive);
    b.text(atlas, kFontSmall, x + pad + 12.f * s, baseline, label, ctx.replay ? kInk : kWhite);
    if (ctx.replay) {
      // Progress bar under the bug shows how much of the replay is left.
      const float pw = w * static_cast<float>(std::clamp(ctx.replay_progress, 0.0, 1.0));
      b.rect(x, y + h, pw, 3.f * s, kWhite);
      b.text(atlas, kFontSmall, x, y + h + 26.f * s, "SLOW MOTION", rgba(255, 255, 255, 230));
    }
  }

  // ---- Lower third (slides in from the left, fades out) -------------------
  if (!st.lower_third.empty() && ctx.out_time >= st.lower_third_start &&
      ctx.out_time < st.lower_third_end + 0.4) {
    const double t_in = (ctx.out_time - st.lower_third_start) / 0.45;
    const double t_out = (ctx.out_time - st.lower_third_end) / 0.4;
    const double alpha = t_out > 0.0 ? 1.0 - std::clamp(t_out, 0.0, 1.0) : 1.0;
    const float h = 54.f * s;
    const float accent_w = 10.f * s;
    const float text_w = static_cast<float>(atlas.text_width(kFontCaption, st.lower_third));
    const float w = text_w + 2.f * 22.f * s + accent_w;
    const float target_x = 60.f * s;
    const float x = target_x - (1.f - static_cast<float>(ease_out_cubic(t_in))) * (w + target_x);
    const float y = static_cast<float>(ctx.height) - 110.f * s;
    const int asc = atlas.line_ascent(kFontCaption);
    const float baseline = y + (h + static_cast<float>(asc) * 0.72f) / 2.f;
    b.rect(x, y, accent_w, h, with_alpha(kAccent, alpha));
    b.rect(x + accent_w, y, w - accent_w, h, with_alpha(kPanel, alpha));
    b.text(atlas, kFontCaption, x + accent_w + 22.f * s, baseline, st.lower_third,
           with_alpha(kWhite, alpha));
  }
}

}  // namespace md
