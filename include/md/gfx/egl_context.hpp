#pragma once

#include <string>

namespace md {

// Headless OpenGL ES 3 context (no window, no display server) via EGL's
// surfaceless platform. Rendering goes to framebuffer objects only.
//
// `driver`: "d3d12" = the real GPU under WSL2 (Mesa's D3D12 Gallium driver),
// "llvmpipe" = CPU software rasteriser (CI machines), "auto" = d3d12 if
// /dev/dxg exists, else the system default.
//
// A GL context is bound to one thread: construct, use and destroy this object
// on the compositor thread only.
class EglContext {
 public:
  explicit EglContext(const std::string& driver = "auto");
  ~EglContext();
  EglContext(const EglContext&) = delete;
  EglContext& operator=(const EglContext&) = delete;

  [[nodiscard]] const std::string& renderer() const { return renderer_; }
  [[nodiscard]] const std::string& version() const { return version_; }

 private:
  void* display_{nullptr};
  void* context_{nullptr};
  std::string renderer_;
  std::string version_;
};

}  // namespace md
