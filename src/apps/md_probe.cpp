// md_probe: prints the properties of a video file that matter for Match
// Director (resolution, frame rate, codec, pixel format, duration).
// Used for the dataset check in docs/dataset_status.md.
//
// Usage: md_probe <file> [<file> ...]

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

#include <cstdio>
#include <memory>
#include <string>

namespace {

// RAII owner for AVFormatContext. avformat_close_input() needs a pointer to
// the pointer, so a plain function-pointer deleter won't do.
struct FormatCloser {
  void operator()(AVFormatContext* ctx) const noexcept { avformat_close_input(&ctx); }
};
using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;

std::string av_error_string(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof(buf));
  return buf;
}

double seconds(int64_t ts, AVRational tb) { return static_cast<double>(ts) * av_q2d(tb); }

const char* media_type_name(AVMediaType t) {
  const char* name = av_get_media_type_string(t);
  return name != nullptr ? name : "unknown";
}

bool probe(const char* path) {
  AVFormatContext* raw = nullptr;
  if (int err = avformat_open_input(&raw, path, nullptr, nullptr); err < 0) {
    std::fprintf(stderr, "%s: cannot open: %s\n", path, av_error_string(err).c_str());
    return false;
  }
  FormatPtr fmt(raw);

  if (int err = avformat_find_stream_info(fmt.get(), nullptr); err < 0) {
    std::fprintf(stderr, "%s: no stream info: %s\n", path, av_error_string(err).c_str());
    return false;
  }

  std::printf("file: %s\n", path);
  std::printf("  container: %s\n", fmt->iformat->name);
  if (fmt->duration != AV_NOPTS_VALUE) {
    std::printf("  duration: %.3f s\n", seconds(fmt->duration, AVRational{1, AV_TIME_BASE}));
  }
  std::printf("  bitrate: %lld kb/s\n", static_cast<long long>(fmt->bit_rate / 1000));

  for (unsigned i = 0; i < fmt->nb_streams; ++i) {
    const AVStream* st = fmt->streams[i];
    const AVCodecParameters* par = st->codecpar;
    const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);
    std::printf("  stream %u: %s, codec=%s\n", i, media_type_name(par->codec_type),
                desc != nullptr ? desc->name : "unknown");

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
      const char* pix = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format));
      std::printf("    resolution: %dx%d\n", par->width, par->height);
      std::printf("    pixel format: %s\n", pix != nullptr ? pix : "unknown");
      std::printf("    avg fps: %.3f (r_frame_rate %d/%d)\n", av_q2d(st->avg_frame_rate),
                  st->r_frame_rate.num, st->r_frame_rate.den);
      std::printf("    frames (container header): %lld\n", static_cast<long long>(st->nb_frames));
      if (st->duration != AV_NOPTS_VALUE) {
        std::printf("    stream duration: %.3f s\n", seconds(st->duration, st->time_base));
      }
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
      std::printf("    sample rate: %d Hz, channels: %d\n", par->sample_rate,
                  par->ch_layout.nb_channels);
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <video file> [...]\n", argv[0]);
    return 2;
  }
  av_log_set_level(AV_LOG_ERROR);
  int failures = 0;
  for (int i = 1; i < argc; ++i) {
    if (!probe(argv[i])) {
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
