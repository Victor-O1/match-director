#pragma once

// RAII owners for FFmpeg objects. FFmpeg's free functions take a pointer to
// the pointer (so they can null it), which is why these are functors rather
// than plain function pointers.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

#include <memory>
#include <stdexcept>
#include <string>

namespace md {

struct AvFrameFree {
  void operator()(AVFrame* f) const noexcept { av_frame_free(&f); }
};
struct AvPacketFree {
  void operator()(AVPacket* p) const noexcept { av_packet_free(&p); }
};
struct AvCodecFree {
  void operator()(AVCodecContext* c) const noexcept { avcodec_free_context(&c); }
};
struct AvInputClose {
  void operator()(AVFormatContext* c) const noexcept { avformat_close_input(&c); }
};

using AvFramePtr = std::unique_ptr<AVFrame, AvFrameFree>;
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketFree>;
using AvCodecPtr = std::unique_ptr<AVCodecContext, AvCodecFree>;
using AvInputPtr = std::unique_ptr<AVFormatContext, AvInputClose>;

[[nodiscard]] inline std::string av_err(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof(buf));
  return buf;
}

// Throws std::runtime_error with context when an FFmpeg call fails.
inline int av_check(int ret, const char* what) {
  if (ret < 0) {
    throw std::runtime_error(std::string(what) + ": " + av_err(ret));
  }
  return ret;
}

[[nodiscard]] inline AvFramePtr make_frame() {
  AvFramePtr f(av_frame_alloc());
  if (!f) {
    throw std::bad_alloc();
  }
  return f;
}

[[nodiscard]] inline AvPacketPtr make_packet() {
  AvPacketPtr p(av_packet_alloc());
  if (!p) {
    throw std::bad_alloc();
  }
  return p;
}

}  // namespace md
