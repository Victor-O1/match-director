#pragma once

#include "md/director/crop.hpp"
#include "md/gfx/egl_context.hpp"
#include "md/gfx/glyph_atlas.hpp"
#include "md/gfx/overlay.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

struct AVFrame;

namespace md {

struct CompositorOptions {
  int out_width{1280};  // must be a multiple of 4 (NV12 packing)
  int out_height{720};  // must be even
  std::string driver{"auto"};
  std::string font_path{"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"};
  bool upload_crop_only{true};  // upload only the crop window of the source planes
  bool async_readback{true};    // double-buffered PBO readback (adds 1 frame of delay)
  bool supersample{true};       // 4-tap filtering when the crop is minified
};

// GPU compositor (OpenGL ES 3.0, headless).
//
// Per frame:
//   1. Upload the source YUV420P planes into three R8 textures. By default only
//      the virtual camera's crop window (plus a 2 px margin) is uploaded, so a
//      4K panorama costs about as much bandwidth as the crop, not the full frame.
//   2. "Program" pass: draw the crop scaled to the output size, converting
//      BT.601 YUV -> RGB in the fragment shader, then draw the graphics layer
//      (one batched draw call from OverlayBatch) with alpha blending.
//   3. Convert RGB -> NV12 on the GPU with two packing passes (each RGBA8 texel
//      carries 4 luma bytes, or 2 interleaved UV pairs), so the readback is
//      already in the encoder's layout and is 1.5 bytes/pixel instead of 4.
//   4. Read back. With async_readback, glReadPixels targets a pixel-buffer
//      object and the CPU maps the PREVIOUS frame's PBO, so the CPU never waits
//      for the GPU to finish the current frame.
class Compositor {
 public:
  Compositor(int source_width, int source_height, CompositorOptions opts);
  ~Compositor();
  Compositor(const Compositor&) = delete;
  Compositor& operator=(const Compositor&) = delete;

  // Renders `src` (YUV420P) through `crop` with the overlay batch on top.
  // `fade` multiplies the video (0 = black) for dip-to-black transitions.
  // Returns true if a finished NV12 frame was written into `out` this call.
  // In async mode the frame written is the one submitted on the PREVIOUS call
  // (so the caller must keep that frame's metadata one step).
  bool render(const AVFrame* src, const RectF& crop, float fade, const OverlayBatch& overlay,
              AVFrame* out);

  // Async mode: retrieve the last submitted frame (end of stream).
  bool flush(AVFrame* out);

  [[nodiscard]] const GlyphAtlas& atlas() const { return *atlas_; }
  [[nodiscard]] const std::string& renderer() const { return egl_.renderer(); }
  [[nodiscard]] const CompositorOptions& options() const { return opts_; }

 private:
  void upload(const AVFrame* src, const RectF& crop);
  void draw_program(const RectF& crop, float fade, const OverlayBatch& overlay);
  void pack_nv12();
  void read_into(AVFrame* out);
  void read_into_pbo(int slot);
  void copy_pbo_to(int slot, AVFrame* out);

  CompositorOptions opts_;
  EglContext egl_;
  std::unique_ptr<GlyphAtlas> atlas_;
  int src_w_;
  int src_h_;

  unsigned prog_video_{0}, prog_overlay_{0}, prog_pack_y_{0}, prog_pack_uv_{0};
  std::array<unsigned, 3> tex_yuv_{};
  unsigned tex_atlas_{0};
  unsigned tex_program_{0}, fbo_program_{0};
  unsigned tex_y_{0}, fbo_y_{0};
  unsigned tex_uv_{0}, fbo_uv_{0};
  unsigned vao_quad_{0}, vbo_quad_{0};
  unsigned vao_overlay_{0}, vbo_overlay_{0};
  std::size_t overlay_capacity_{0};
  std::array<unsigned, 2> pbo_{};
  int pbo_next_{0};
  bool pbo_pending_{false};

  // Uniform locations.
  int u_crop_{-1}, u_fade_{-1}, u_texel_{-1}, u_ss_{-1};
  int u_size_{-1};
};

}  // namespace md
