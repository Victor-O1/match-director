#pragma once

#include "md/media/av_ptr.hpp"

#include <string>

namespace md {

struct OutputOptions {
  std::string url;  // file path, rtmp://..., srt://..., udp://...
  int width{1280};
  int height{720};
  AVRational fps{25, 1};
  std::string preset{"veryfast"};
  int crf{21};                            // used for files
  int bitrate_kbps{0};                    // > 0: constant bitrate (use for streaming)
  bool zerolatency{false};                // x264 tune=zerolatency (no lookahead / B-frames)
  const AVStream* audio_source{nullptr};  // copy this audio stream (no re-encode), optional
};

// H.264 (libx264) encoder + muxer. Container picked from the URL scheme:
// rtmp -> FLV, srt/udp -> MPEG-TS, otherwise from the file extension.
//
// Used by the encoder thread only.
class ProgramOutput {
 public:
  explicit ProgramOutput(const OutputOptions& opts);
  ~ProgramOutput();
  ProgramOutput(const ProgramOutput&) = delete;
  ProgramOutput& operator=(const ProgramOutput&) = delete;

  // Encodes one NV12 frame at output frame index `index` (pts in 1/fps units).
  void write_video(AVFrame* frame, std::int64_t index);
  // Copies an audio packet; `pkt->pts/dts` must already be in the audio
  // SOURCE time base (the output stream's time base may differ).
  void write_audio(AVPacket* pkt);
  // Flushes the encoder and writes the trailer. Safe to call twice.
  void finish();

  [[nodiscard]] std::int64_t bytes_written() const;
  [[nodiscard]] std::int64_t packets_written() const { return packets_; }

 private:
  void drain(bool flushing);

  OutputOptions opts_;
  AVFormatContext* fmt_{nullptr};
  AvCodecPtr enc_;
  AVStream* vstream_{nullptr};
  AVStream* astream_{nullptr};
  AVRational audio_src_tb_{1, 1};
  AvPacketPtr pkt_;
  bool header_written_{false};
  bool finished_{false};
  std::int64_t packets_{0};
};

// Allocates an NV12 frame with 32-byte aligned rows (multiple of 4 for the
// compositor's packed readback).
[[nodiscard]] AvFramePtr make_nv12_frame(int width, int height);

}  // namespace md
