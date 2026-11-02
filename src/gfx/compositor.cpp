#include "md/gfx/compositor.hpp"

#include <GLES3/gl3.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace md {

namespace {

// ---------------------------------------------------------------------------
// Shaders (GLSL ES 3.00). highp everywhere: on many GPUs mediump is fp16,
// whose 10-bit mantissa cannot address texels of a 3840-wide texture.
// ---------------------------------------------------------------------------

constexpr const char* kQuadVs = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;      // -1..1
uniform vec4 u_crop;                     // u0, v0, u1, v1 (texture coords)
out vec2 v_uv;
void main() {
  vec2 t = a_pos * 0.5 + 0.5;
  // Framebuffer row 0 is image row 0 (top): no flips anywhere, so the
  // readback is in top-down memory order like the decoder's frames.
  v_uv = vec2(mix(u_crop.x, u_crop.z, t.x), mix(u_crop.y, u_crop.w, t.y));
  gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

constexpr const char* kVideoFs = R"(#version 300 es
precision highp float;
uniform sampler2D u_y;
uniform sampler2D u_u;
uniform sampler2D u_v;
uniform float u_fade;
uniform vec2 u_texel;   // source uv covered by one output pixel
uniform float u_ss;     // 1 = supersample (crop is being minified)
in vec2 v_uv;
out vec4 o_color;

vec3 yuv(vec2 uv) {
  return vec3(texture(u_y, uv).r, texture(u_u, uv).r, texture(u_v, uv).r);
}

void main() {
  vec3 c;
  if (u_ss > 0.5) {
    // 4 bilinear taps spread over the output pixel's footprint ~ 4x4 box filter:
    // reduces shimmer on grass and lines when 2-3 source px map to 1 output px.
    vec2 d = 0.25 * u_texel;
    c = 0.25 * (yuv(v_uv + vec2(-d.x, -d.y)) + yuv(v_uv + vec2(d.x, -d.y)) +
                yuv(v_uv + vec2(-d.x, d.y)) + yuv(v_uv + vec2(d.x, d.y)));
  } else {
    c = yuv(v_uv);
  }
  // BT.601 limited range -> full-range RGB.
  float y = (c.x - 16.0 / 255.0) * (255.0 / 219.0);
  float u = (c.y - 128.0 / 255.0) * (255.0 / 224.0);
  float v = (c.z - 128.0 / 255.0) * (255.0 / 224.0);
  vec3 rgb = vec3(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u);
  o_color = vec4(clamp(rgb, 0.0, 1.0) * u_fade, 1.0);
}
)";

constexpr const char* kOverlayVs = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;     // output pixels, top-left origin
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;   // normalised bytes
layout(location = 3) in float a_mode;
uniform vec2 u_size;
out vec2 v_uv;
out vec4 v_color;
out float v_mode;
void main() {
  v_uv = a_uv;
  v_color = a_color;
  v_mode = a_mode;
  gl_Position = vec4(a_pos / u_size * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kOverlayFs = R"(#version 300 es
precision highp float;
uniform sampler2D u_atlas;
in vec2 v_uv;
in vec4 v_color;
in float v_mode;
out vec4 o_color;
void main() {
  float a = v_mode > 0.5 ? texture(u_atlas, v_uv).r : 1.0;
  o_color = vec4(v_color.rgb, v_color.a * a);
}
)";

constexpr const char* kPackVs = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
)";

// Each output texel = 4 consecutive luma samples (BT.601 limited range).
constexpr const char* kPackYFs = R"(#version 300 es
precision highp float;
uniform sampler2D u_program;
out vec4 o;
float luma(vec3 c) { return (16.0 + 65.481 * c.r + 128.553 * c.g + 24.966 * c.b) / 255.0; }
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  int x = p.x * 4;
  o = vec4(luma(texelFetch(u_program, ivec2(x, p.y), 0).rgb),
           luma(texelFetch(u_program, ivec2(x + 1, p.y), 0).rgb),
           luma(texelFetch(u_program, ivec2(x + 2, p.y), 0).rgb),
           luma(texelFetch(u_program, ivec2(x + 3, p.y), 0).rgb));
}
)";

// Each output texel = two NV12 chroma pairs (U0 V0 U1 V1); each chroma sample
// averages a 2x2 block of RGB pixels.
constexpr const char* kPackUvFs = R"(#version 300 es
precision highp float;
uniform sampler2D u_program;
out vec4 o;
vec3 avg2x2(ivec2 tl) {
  return 0.25 * (texelFetch(u_program, tl, 0).rgb + texelFetch(u_program, tl + ivec2(1, 0), 0).rgb +
                 texelFetch(u_program, tl + ivec2(0, 1), 0).rgb + texelFetch(u_program, tl + ivec2(1, 1), 0).rgb);
}
vec2 uv(vec3 c) {
  return vec2((128.0 - 37.797 * c.r - 74.203 * c.g + 112.0 * c.b) / 255.0,
              (128.0 + 112.0 * c.r - 93.786 * c.g - 18.214 * c.b) / 255.0);
}
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec2 a = uv(avg2x2(ivec2(p.x * 4, p.y * 2)));
  vec2 b = uv(avg2x2(ivec2(p.x * 4 + 2, p.y * 2)));
  o = vec4(a, b);
}
)";

GLuint compile(GLenum type, const char* src) {
  const GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (ok == 0) {
    char log[2048] = {};
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    glDeleteShader(s);
    throw std::runtime_error(std::string("shader compile failed: ") + log);
  }
  return s;
}

GLuint link(const char* vs, const char* fs) {
  const GLuint v = compile(GL_VERTEX_SHADER, vs);
  const GLuint f = compile(GL_FRAGMENT_SHADER, fs);
  const GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (ok == 0) {
    char log[2048] = {};
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    glDeleteProgram(p);
    throw std::runtime_error(std::string("program link failed: ") + log);
  }
  return p;
}

GLuint make_texture(GLint internal, GLenum format, int w, int h, GLint filter, const void* data) {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, data);
  return t;
}

GLuint make_fbo(GLuint tex) {
  GLuint f = 0;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    throw std::runtime_error("framebuffer incomplete");
  }
  return f;
}

void check_gl(const char* where) {
  const GLenum e = glGetError();
  if (e != GL_NO_ERROR) {
    throw std::runtime_error(std::string("GL error 0x") + std::to_string(e) + " at " + where);
  }
}

std::vector<int> font_sizes_for(int out_height) {
  const double s = out_height / 720.0;
  return {static_cast<int>(std::lround(24 * s)), static_cast<int>(std::lround(28 * s)),
          static_cast<int>(std::lround(17 * s))};
}

}  // namespace

Compositor::Compositor(int source_width, int source_height, CompositorOptions opts)
    : opts_(std::move(opts)), egl_(opts_.driver), src_w_(source_width), src_h_(source_height) {
  if (opts_.out_width % 4 != 0 || opts_.out_height % 2 != 0) {
    throw std::invalid_argument("output width must be a multiple of 4 and height even");
  }
  atlas_ = std::make_unique<GlyphAtlas>(opts_.font_path, font_sizes_for(opts_.out_height));

  prog_video_ = link(kQuadVs, kVideoFs);
  prog_overlay_ = link(kOverlayVs, kOverlayFs);
  prog_pack_y_ = link(kPackVs, kPackYFs);
  prog_pack_uv_ = link(kPackVs, kPackUvFs);

  glUseProgram(prog_video_);
  glUniform1i(glGetUniformLocation(prog_video_, "u_y"), 0);
  glUniform1i(glGetUniformLocation(prog_video_, "u_u"), 1);
  glUniform1i(glGetUniformLocation(prog_video_, "u_v"), 2);
  u_crop_ = glGetUniformLocation(prog_video_, "u_crop");
  u_fade_ = glGetUniformLocation(prog_video_, "u_fade");
  u_texel_ = glGetUniformLocation(prog_video_, "u_texel");
  u_ss_ = glGetUniformLocation(prog_video_, "u_ss");
  glUseProgram(prog_overlay_);
  glUniform1i(glGetUniformLocation(prog_overlay_, "u_atlas"), 3);
  u_size_ = glGetUniformLocation(prog_overlay_, "u_size");
  glUniform2f(u_size_, static_cast<float>(opts_.out_width), static_cast<float>(opts_.out_height));
  for (GLuint p : {prog_pack_y_, prog_pack_uv_}) {
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_program"), 4);
  }

  // Source planes: allocated once at full size; per frame only sub-rectangles
  // are updated.
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  tex_yuv_[0] = make_texture(GL_R8, GL_RED, src_w_, src_h_, GL_LINEAR, nullptr);
  tex_yuv_[1] = make_texture(GL_R8, GL_RED, src_w_ / 2, src_h_ / 2, GL_LINEAR, nullptr);
  tex_yuv_[2] = make_texture(GL_R8, GL_RED, src_w_ / 2, src_h_ / 2, GL_LINEAR, nullptr);
  tex_atlas_ = make_texture(GL_R8, GL_RED, atlas_->width(), atlas_->height(), GL_LINEAR,
                            atlas_->pixels().data());

  const int w = opts_.out_width;
  const int h = opts_.out_height;
  tex_program_ = make_texture(GL_RGBA8, GL_RGBA, w, h, GL_NEAREST, nullptr);
  fbo_program_ = make_fbo(tex_program_);
  tex_y_ = make_texture(GL_RGBA8, GL_RGBA, w / 4, h, GL_NEAREST, nullptr);
  fbo_y_ = make_fbo(tex_y_);
  tex_uv_ = make_texture(GL_RGBA8, GL_RGBA, w / 4, h / 2, GL_NEAREST, nullptr);
  fbo_uv_ = make_fbo(tex_uv_);

  // Bind each texture to a fixed unit once; nothing rebinds them later.
  for (int i = 0; i < 3; ++i) {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
    glBindTexture(GL_TEXTURE_2D, tex_yuv_[static_cast<std::size_t>(i)]);
  }
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, tex_atlas_);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, tex_program_);

  // Full-screen quad as a triangle strip.
  const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
  glGenVertexArrays(1, &vao_quad_);
  glBindVertexArray(vao_quad_);
  glGenBuffers(1, &vbo_quad_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_quad_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  glGenVertexArrays(1, &vao_overlay_);
  glBindVertexArray(vao_overlay_);
  glGenBuffers(1, &vbo_overlay_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_overlay_);
  overlay_capacity_ = 6 * 1024;
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(overlay_capacity_ * sizeof(OverlayVertex)),
               nullptr, GL_STREAM_DRAW);
  const auto stride = static_cast<GLsizei>(sizeof(OverlayVertex));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(OverlayVertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(OverlayVertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                        reinterpret_cast<const void*>(offsetof(OverlayVertex, rgba)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(OverlayVertex, mode)));
  glBindVertexArray(0);

  if (opts_.async_readback) {
    const auto bytes = static_cast<GLsizeiptr>(w * h + w * h / 2);
    glGenBuffers(2, pbo_.data());
    for (GLuint p : pbo_) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, p);
      glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  }
  check_gl("Compositor init");
}

Compositor::~Compositor() {
  // The EGL context is still current here (egl_ is destroyed after this body).
  glDeleteBuffers(2, pbo_.data());
  glDeleteBuffers(1, &vbo_overlay_);
  glDeleteBuffers(1, &vbo_quad_);
  glDeleteVertexArrays(1, &vao_overlay_);
  glDeleteVertexArrays(1, &vao_quad_);
  const GLuint fbos[] = {fbo_program_, fbo_y_, fbo_uv_};
  glDeleteFramebuffers(3, fbos);
  const GLuint texs[] = {tex_yuv_[0],  tex_yuv_[1], tex_yuv_[2], tex_atlas_,
                         tex_program_, tex_y_,      tex_uv_};
  glDeleteTextures(7, texs);
  for (GLuint p : {prog_video_, prog_overlay_, prog_pack_y_, prog_pack_uv_}) {
    glDeleteProgram(p);
  }
}

void Compositor::upload(const AVFrame* src, const RectF& crop) {
  // Region to upload in luma pixels: the crop plus a margin for bilinear
  // filtering and supersampling taps, aligned to even coordinates so the
  // chroma region (half size) covers exactly the same area.
  int x0 = 0;
  int y0 = 0;
  int x1 = src_w_;
  int y1 = src_h_;
  if (opts_.upload_crop_only) {
    x0 = std::max(0, (static_cast<int>(std::floor(crop.x)) - 4) & ~1);
    y0 = std::max(0, (static_cast<int>(std::floor(crop.y)) - 4) & ~1);
    x1 = std::min(src_w_, (static_cast<int>(std::ceil(crop.x + crop.width)) + 5) & ~1);
    y1 = std::min(src_h_, (static_cast<int>(std::ceil(crop.y + crop.height)) + 5) & ~1);
  }
  for (int plane = 0; plane < 3; ++plane) {
    const int shift = plane == 0 ? 0 : 1;
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + plane));
    // UNPACK_* lets GL read a sub-rectangle straight out of the decoder's
    // padded plane: no CPU-side copy into a tight buffer.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, src->linesize[plane]);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, x0 >> shift);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, y0 >> shift);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x0 >> shift, y0 >> shift, (x1 - x0) >> shift,
                    (y1 - y0) >> shift, GL_RED, GL_UNSIGNED_BYTE, src->data[plane]);
  }
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
}

void Compositor::draw_program(const RectF& crop, float fade, const OverlayBatch& overlay) {
  const int w = opts_.out_width;
  const int h = opts_.out_height;
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_program_);
  glViewport(0, 0, w, h);
  glDisable(GL_BLEND);

  glUseProgram(prog_video_);
  const auto sw = static_cast<float>(src_w_);
  const auto sh = static_cast<float>(src_h_);
  glUniform4f(u_crop_, static_cast<float>(crop.x) / sw, static_cast<float>(crop.y) / sh,
              static_cast<float>(crop.x + crop.width) / sw,
              static_cast<float>(crop.y + crop.height) / sh);
  glUniform1f(u_fade_, fade);
  glUniform2f(u_texel_, static_cast<float>(crop.width) / static_cast<float>(w) / sw,
              static_cast<float>(crop.height) / static_cast<float>(h) / sh);
  const bool minified = crop.width > 1.2 * w;
  glUniform1f(u_ss_, opts_.supersample && minified ? 1.f : 0.f);
  glBindVertexArray(vao_quad_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  const auto& verts = overlay.vertices();
  if (!verts.empty()) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_overlay_);
    glBindVertexArray(vao_overlay_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_overlay_);
    if (verts.size() > overlay_capacity_) {
      overlay_capacity_ = verts.size() * 2;
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(overlay_capacity_ * sizeof(OverlayVertex)), nullptr,
                   GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(verts.size() * sizeof(OverlayVertex)), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glDisable(GL_BLEND);
  }
}

void Compositor::pack_nv12() {
  const int w = opts_.out_width;
  const int h = opts_.out_height;
  glBindVertexArray(vao_quad_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_y_);
  glViewport(0, 0, w / 4, h);
  glUseProgram(prog_pack_y_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_uv_);
  glViewport(0, 0, w / 4, h / 2);
  glUseProgram(prog_pack_uv_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Compositor::read_into(AVFrame* out) {
  const int w = opts_.out_width;
  const int h = opts_.out_height;
  // Row length is in texels (4 bytes each); linesize is a multiple of 32.
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_y_);
  glPixelStorei(GL_PACK_ROW_LENGTH, out->linesize[0] / 4);
  glReadPixels(0, 0, w / 4, h, GL_RGBA, GL_UNSIGNED_BYTE, out->data[0]);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_uv_);
  glPixelStorei(GL_PACK_ROW_LENGTH, out->linesize[1] / 4);
  glReadPixels(0, 0, w / 4, h / 2, GL_RGBA, GL_UNSIGNED_BYTE, out->data[1]);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}

void Compositor::read_into_pbo(int slot) {
  const int w = opts_.out_width;
  const int h = opts_.out_height;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[static_cast<std::size_t>(slot)]);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_y_);
  glReadPixels(0, 0, w / 4, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_uv_);
  glReadPixels(0, 0, w / 4, h / 2, GL_RGBA, GL_UNSIGNED_BYTE,
               reinterpret_cast<void*>(static_cast<std::uintptr_t>(w * h)));
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void Compositor::copy_pbo_to(int slot, AVFrame* out) {
  const int w = opts_.out_width;
  const int h = opts_.out_height;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[static_cast<std::size_t>(slot)]);
  const auto bytes = static_cast<GLsizeiptr>(w * h + w * h / 2);
  const auto* src = static_cast<const std::uint8_t*>(
      glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT));
  if (src == nullptr) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    throw std::runtime_error("glMapBufferRange failed");
  }
  const auto row = static_cast<std::size_t>(w);
  for (int y = 0; y < h; ++y) {
    std::memcpy(out->data[0] + static_cast<std::ptrdiff_t>(y) * out->linesize[0],
                src + static_cast<std::size_t>(y) * row, row);
  }
  const std::uint8_t* uv = src + static_cast<std::size_t>(w * h);
  for (int y = 0; y < h / 2; ++y) {
    std::memcpy(out->data[1] + static_cast<std::ptrdiff_t>(y) * out->linesize[1],
                uv + static_cast<std::size_t>(y) * row, row);
  }
  glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

bool Compositor::render(const AVFrame* src, const RectF& crop, float fade,
                        const OverlayBatch& overlay, AVFrame* out) {
  upload(src, crop);
  draw_program(crop, fade, overlay);
  pack_nv12();

  if (!opts_.async_readback) {
    read_into(out);
    check_gl("render (sync)");
    return true;
  }
  const int slot = pbo_next_;
  read_into_pbo(slot);  // queued on the GPU; returns immediately
  bool produced = false;
  if (pbo_pending_) {
    copy_pbo_to(1 - slot, out);  // previous frame: almost certainly finished by now
    produced = true;
  }
  pbo_pending_ = true;
  pbo_next_ = 1 - slot;
  check_gl("render (async)");
  return produced;
}

bool Compositor::flush(AVFrame* out) {
  if (!opts_.async_readback || !pbo_pending_) {
    return false;
  }
  copy_pbo_to(1 - pbo_next_, out);
  pbo_pending_ = false;
  return true;
}

}  // namespace md
