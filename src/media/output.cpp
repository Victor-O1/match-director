#include "md/media/output.hpp"

extern "C" {
#include <libavutil/opt.h>
}

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace md {

namespace {

const char* container_for(std::string_view url) {
  if (url.rfind("rtmp://", 0) == 0 || url.rfind("rtmps://", 0) == 0) return "flv";
  if (url.rfind("srt://", 0) == 0 || url.rfind("udp://", 0) == 0) return "mpegts";
  return nullptr;  // guess from extension
}

}  // namespace

AvFramePtr make_nv12_frame(int width, int height) {
  AvFramePtr f = make_frame();
  f->format = AV_PIX_FMT_NV12;
  f->width = width;
  f->height = height;
  av_check(av_frame_get_buffer(f.get(), 32), "alloc NV12 frame");
  return f;
}

ProgramOutput::ProgramOutput(const OutputOptions& opts) : opts_(opts), pkt_(make_packet()) {
  av_check(
      avformat_alloc_output_context2(&fmt_, nullptr, container_for(opts_.url), opts_.url.c_str()),
      "alloc output");

  const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
  if (codec == nullptr) {
    throw std::runtime_error("libx264 encoder not available in this FFmpeg build");
  }
  enc_.reset(avcodec_alloc_context3(codec));
  AVCodecContext* c = enc_.get();
  c->width = opts_.width;
  c->height = opts_.height;
  c->pix_fmt = AV_PIX_FMT_NV12;
  c->time_base = av_inv_q(opts_.fps);
  c->framerate = opts_.fps;
  c->gop_size = 2 * opts_.fps.num / std::max(1, opts_.fps.den);  // keyframe every 2 s
  // Same colour description as the input (BT.601 limited) so players don't shift colours.
  c->color_range = AVCOL_RANGE_MPEG;
  c->colorspace = AVCOL_SPC_BT470BG;
  c->color_primaries = AVCOL_PRI_BT470BG;
  c->color_trc = AVCOL_TRC_SMPTE170M;
  if ((fmt_->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  av_opt_set(c->priv_data, "preset", opts_.preset.c_str(), 0);
  if (opts_.zerolatency) {
    av_opt_set(c->priv_data, "tune", "zerolatency", 0);
  }
  if (opts_.bitrate_kbps > 0) {
    c->bit_rate = static_cast<std::int64_t>(opts_.bitrate_kbps) * 1000;
    c->rc_max_rate = c->bit_rate;
    c->rc_buffer_size = static_cast<int>(c->bit_rate);  // 1 s VBV
  } else {
    av_opt_set_int(c->priv_data, "crf", opts_.crf, 0);
  }
  av_check(avcodec_open2(c, codec, nullptr), "open libx264");

  vstream_ = avformat_new_stream(fmt_, nullptr);
  av_check(avcodec_parameters_from_context(vstream_->codecpar, c), "video stream params");
  vstream_->time_base = c->time_base;

  if (opts_.audio_source != nullptr) {
    astream_ = avformat_new_stream(fmt_, nullptr);
    av_check(avcodec_parameters_copy(astream_->codecpar, opts_.audio_source->codecpar),
             "audio stream params");
    astream_->codecpar->codec_tag = 0;
    astream_->time_base = opts_.audio_source->time_base;
    audio_src_tb_ = opts_.audio_source->time_base;
  }

  if ((fmt_->oformat->flags & AVFMT_NOFILE) == 0) {
    av_check(avio_open(&fmt_->pb, opts_.url.c_str(), AVIO_FLAG_WRITE), "open output");
  }
  av_check(avformat_write_header(fmt_, nullptr), "write header");
  header_written_ = true;
}

ProgramOutput::~ProgramOutput() {
  try {
    finish();
  } catch (...) {
    // Destructors must not throw; finish() errors were already reported if
    // the caller called it explicitly.
  }
  if (fmt_ != nullptr) {
    if ((fmt_->oformat->flags & AVFMT_NOFILE) == 0) {
      avio_closep(&fmt_->pb);
    }
    avformat_free_context(fmt_);
  }
}

void ProgramOutput::write_video(AVFrame* frame, std::int64_t index) {
  frame->pts = index;
  av_check(avcodec_send_frame(enc_.get(), frame), "encode");
  drain(false);
}

void ProgramOutput::write_audio(AVPacket* pkt) {
  if (astream_ == nullptr) {
    return;
  }
  pkt->stream_index = astream_->index;
  av_packet_rescale_ts(pkt, audio_src_tb_, astream_->time_base);
  pkt->pos = -1;
  av_check(av_interleaved_write_frame(fmt_, pkt), "write audio");
  ++packets_;
}

void ProgramOutput::drain(bool flushing) {
  for (;;) {
    const int ret = avcodec_receive_packet(enc_.get(), pkt_.get());
    if (ret == AVERROR(EAGAIN) || (flushing && ret == AVERROR_EOF)) {
      return;
    }
    av_check(ret, "receive packet");
    pkt_->stream_index = vstream_->index;
    av_packet_rescale_ts(pkt_.get(), enc_->time_base, vstream_->time_base);
    av_check(av_interleaved_write_frame(fmt_, pkt_.get()), "write video");
    ++packets_;
  }
}

void ProgramOutput::finish() {
  if (finished_ || !header_written_) {
    return;
  }
  finished_ = true;
  av_check(avcodec_send_frame(enc_.get(), nullptr), "flush encoder");
  drain(true);
  av_check(av_write_trailer(fmt_), "write trailer");
}

std::int64_t ProgramOutput::bytes_written() const {
  return fmt_ != nullptr && fmt_->pb != nullptr ? avio_tell(fmt_->pb) : 0;
}

}  // namespace md
