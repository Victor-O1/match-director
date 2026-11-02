#include "md/media/source.hpp"

extern "C" {
#include <libavutil/avutil.h>
}

#include <cmath>
#include <stdexcept>

namespace md {

MediaSource::MediaSource(const std::string& url, int decode_threads) {
  AVFormatContext* raw = nullptr;
  av_check(avformat_open_input(&raw, url.c_str(), nullptr, nullptr), "open input");
  fmt_.reset(raw);
  av_check(avformat_find_stream_info(fmt_.get(), nullptr), "find stream info");

  video_index_ = av_find_best_stream(fmt_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video_index_ < 0) {
    throw std::runtime_error("no video stream in " + url);
  }
  audio_index_ = av_find_best_stream(fmt_.get(), AVMEDIA_TYPE_AUDIO, -1, video_index_, nullptr, 0);

  const AVStream* vs = video_stream();
  size_ = {vs->codecpar->width, vs->codecpar->height};
  fps_ = av_guess_frame_rate(fmt_.get(), const_cast<AVStream*>(vs), nullptr);
  if (fps_.num <= 0 || fps_.den <= 0) {
    fps_ = {25, 1};
  }
  video_tb_ = vs->time_base;
  if (fmt_->duration != AV_NOPTS_VALUE) {
    duration_ = static_cast<double>(fmt_->duration) / AV_TIME_BASE;
  }
  dec_ = make_decoder(decode_threads);
}

AvCodecPtr MediaSource::make_decoder(int threads) const {
  const AVStream* vs = video_stream();
  const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
  if (codec == nullptr) {
    throw std::runtime_error("no decoder for the video codec");
  }
  AvCodecPtr ctx(avcodec_alloc_context3(codec));
  av_check(avcodec_parameters_to_context(ctx.get(), vs->codecpar), "codec params");
  ctx->pkt_timebase = vs->time_base;
  ctx->thread_count = threads;  // 0 = auto
  // Slice threads add no latency; frame threads add (threads-1) frames of delay
  // but scale much better on 4K. Allow both and let FFmpeg pick per stream.
  ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  av_check(avcodec_open2(ctx.get(), codec, nullptr), "open decoder");
  return ctx;
}

const AVStream* MediaSource::video_stream() const {
  return fmt_->streams[static_cast<unsigned>(video_index_)];
}

const AVStream* MediaSource::audio_stream() const {
  return audio_index_ >= 0 ? fmt_->streams[static_cast<unsigned>(audio_index_)] : nullptr;
}

AVPixelFormat MediaSource::pixel_format() const { return dec_->pix_fmt; }

MediaSource::Kind MediaSource::read(AVPacket* pkt) {
  const int ret = av_read_frame(fmt_.get(), pkt);
  if (ret == AVERROR_EOF) {
    return Kind::End;
  }
  av_check(ret, "read packet");
  if (pkt->stream_index == video_index_) return Kind::Video;
  if (pkt->stream_index == audio_index_) return Kind::Audio;
  return Kind::Other;
}

void MediaSource::seek(double seconds) {
  const auto ts = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
  // Lands on the keyframe at or before ts; the caller discards frames before
  // the exact start, which makes the start frame-accurate.
  av_check(avformat_seek_file(fmt_.get(), -1, INT64_MIN, ts, ts, 0), "seek");
  avcodec_flush_buffers(dec_.get());
}

int MediaSource::send(const AVPacket* pkt) { return avcodec_send_packet(dec_.get(), pkt); }
int MediaSource::receive(AVFrame* frame) { return avcodec_receive_frame(dec_.get(), frame); }

std::int64_t MediaSource::frame_index(std::int64_t pts) const {
  // index = pts * time_base * fps, rounded to nearest.
  return av_rescale_q_rnd(pts, video_tb_, av_inv_q(fps_),
                          static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}

double MediaSource::seconds(std::int64_t pts) const {
  return static_cast<double>(pts) * av_q2d(video_tb_);
}

}  // namespace md
