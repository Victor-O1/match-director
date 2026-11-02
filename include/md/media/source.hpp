#pragma once

#include "md/director/crop.hpp"
#include "md/media/av_ptr.hpp"

#include <string>

namespace md {

// Demuxer + video decoder for one input (file or network URL).
//
// Owned and used by the decode thread only.
class MediaSource {
 public:
  MediaSource(const std::string& url, int decode_threads);

  enum class Kind { Video, Audio, Other, End };
  // Reads the next packet into `pkt` (caller unrefs). Kind tells which stream.
  Kind read(AVPacket* pkt);

  // Seeks so that the next decoded frames start at or before `seconds`.
  void seek(double seconds);

  // Decoder interface (wraps avcodec_send_packet / receive_frame).
  // Returns 0, AVERROR(EAGAIN) or AVERROR_EOF like FFmpeg.
  int send(const AVPacket* pkt);
  int receive(AVFrame* frame);

  [[nodiscard]] Size size() const { return size_; }
  [[nodiscard]] AVRational frame_rate() const { return fps_; }
  [[nodiscard]] AVRational video_time_base() const { return video_tb_; }
  [[nodiscard]] double duration() const { return duration_; }
  [[nodiscard]] const AVStream* audio_stream() const;
  [[nodiscard]] const AVStream* video_stream() const;
  [[nodiscard]] AVPixelFormat pixel_format() const;

  // A second, independent decoder for the same stream (replays decode old
  // packets without disturbing the live decoder's reference frames).
  [[nodiscard]] AvCodecPtr make_decoder(int threads) const;

  // Frame index for a pts in the video time base.
  [[nodiscard]] std::int64_t frame_index(std::int64_t pts) const;
  [[nodiscard]] double seconds(std::int64_t pts) const;

 private:
  AvInputPtr fmt_;
  AvCodecPtr dec_;
  int video_index_{-1};
  int audio_index_{-1};
  Size size_;
  AVRational fps_{25, 1};
  AVRational video_tb_{1, 25};
  double duration_{0.0};
};

}  // namespace md
