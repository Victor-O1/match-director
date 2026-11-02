#include "md/gfx/egl_context.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <unistd.h>

#include <cstdlib>
#include <stdexcept>

namespace md {

namespace {

const char* gl_string(GLenum name) {
  const auto* s = glGetString(name);
  return s != nullptr ? reinterpret_cast<const char*>(s) : "unknown";
}

}  // namespace

EglContext::EglContext(const std::string& driver) {
  // Mesa reads GALLIUM_DRIVER when the display is initialised. Respect a value
  // the user already exported.
  if (std::getenv("GALLIUM_DRIVER") == nullptr) {
    if (driver == "d3d12" || (driver == "auto" && ::access("/dev/dxg", F_OK) == 0)) {
      ::setenv("GALLIUM_DRIVER", "d3d12", 0);
    } else if (driver == "llvmpipe") {
      ::setenv("GALLIUM_DRIVER", "llvmpipe", 0);
    }
  }

  auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
      eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (get_platform_display == nullptr) {
    throw std::runtime_error("EGL: eglGetPlatformDisplayEXT not available");
  }
  EGLDisplay dpy =
      get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  EGLint major = 0;
  EGLint minor = 0;
  if (dpy == EGL_NO_DISPLAY || eglInitialize(dpy, &major, &minor) != EGL_TRUE) {
    throw std::runtime_error("EGL: cannot initialise surfaceless display");
  }
  display_ = dpy;
  eglBindAPI(EGL_OPENGL_ES_API);

  const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
  // No config needed for surfaceless rendering into FBOs (EGL_KHR_no_config_context).
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
  if (ctx == EGL_NO_CONTEXT) {
    eglTerminate(dpy);
    throw std::runtime_error("EGL: cannot create an OpenGL ES 3.0 context");
  }
  context_ = ctx;
  if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx) != EGL_TRUE) {
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    throw std::runtime_error("EGL: eglMakeCurrent failed");
  }
  renderer_ = gl_string(GL_RENDERER);
  version_ = gl_string(GL_VERSION);
}

EglContext::~EglContext() {
  auto dpy = static_cast<EGLDisplay>(display_);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(dpy, static_cast<EGLContext>(context_));
  eglReleaseThread();
  // Deliberately NO eglTerminate(): with Mesa's D3D12 driver (WSL2), terminating
  // the last display dlclose()s the driver, but the driver has registered a
  // thread-local destructor on this thread. When the thread then exits, glibc
  // calls into the unloaded library and the process segfaults (found with gdb,
  // see LESSONS.md). The display is cleaned up at process exit instead.
}

}  // namespace md
