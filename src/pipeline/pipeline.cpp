#include "md/pipeline/pipeline.hpp"

#include "md/core/log.hpp"
#include "md/director/director.hpp"
#include "md/gfx/compositor.hpp"
#include "md/gfx/overlay.hpp"
#include "md/media/output.hpp"
#include "md/media/source.hpp"
#include "md/track/tracker.hpp"
#include "md/vision/detector.hpp"
#include "md/vision/image.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace md {

namespace {

constexpr std::size_t kFramePool = 16;    // decoded pictures in flight
constexpr std::size_t kOutPool = 12;      // NV12 output pictures in flight
constexpr std::size_t kAudioPool = 1024;  // ~24 s of AAC: covers startup + replays
constexpr std::size_t kIngestRing = 256;
constexpr int kFadeFrames = 6;

double peak_rss_mb() {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmHWM:", 0) == 0) {
      return std::stod(line.substr(6)) / 1024.0;  // value is in kB
    }
  }
  return 0.0;
}

// Sorts `v` in place (callers don't need the original order).
double percentile_of(std::vector<double>& v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
  return v[std::clamp<std::size_t>(rank, 1, v.size()) - 1];
}

nlohmann::json latency_json(const LatencySummary& s) {
  auto ms = [](std::int64_t ns) { return static_cast<double>(ns) / 1e6; };
  return {{"count", s.count},       {"p50_ms", ms(s.p50_ns)}, {"p95_ms", ms(s.p95_ns)},
          {"p99_ms", ms(s.p99_ns)}, {"max_ms", ms(s.max_ns)}, {"mean_ms", s.mean_ns / 1e6}};
}

// Binary greyscale PGM: viewable with any image tool, no dependencies.
void write_pgm(const std::string& path, const GrayImage& img) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return;
  std::fprintf(f, "P5\n%d %d\n255\n", img.width, img.height);
  std::fwrite(img.pixels.data(), 1, img.pixels.size(), f);
  std::fclose(f);
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

std::string RunSummary::to_json() const {
  nlohmann::json j;
  j["renderer"] = renderer;
  j["source"] = {{"width", source_size.width}, {"height", source_size.height}, {"fps", fps}};
  j["source_seconds_processed"] = source_seconds;
  j["wall_seconds"] = wall_seconds;
  j["speed_x_realtime"] = wall_seconds > 0 ? source_seconds / wall_seconds : 0.0;
  j["live_frames"] = live_frames;
  j["processing_fps"] = wall_seconds > 0 ? static_cast<double>(live_frames) / wall_seconds : 0.0;
  j["replay_source_frames"] = replay_source_frames;
  j["output_frames"] = output_frames;
  j["audio_packets"] = audio_packets;
  j["audio_dropped"] = audio_dropped;
  j["output_mbytes"] = static_cast<double>(output_bytes) / 1e6;
  j["late_frames"] = late_frames;
  j["encoder_held_frames"] = encoder_held_frames;
  j["latency_ingest_to_output"] = latency_json(latency_total);
  j["work_per_frame"] = {{"decode", latency_json(work_decode)},
                         {"analysis", latency_json(work_analysis)},
                         {"composite", latency_json(work_composite)},
                         {"encode", latency_json(work_encode)}};
  j["director"] = {{"players_tracked_mean", players_mean},
                   {"players_in_view_rate", in_view_rate},
                   {"pan_speed_px_s_mean", pan_speed_mean},
                   {"pan_speed_px_s_p95", pan_speed_p95},
                   {"pan_speed_px_s_max", pan_speed_max},
                   {"jerk_px_s3_mean", jerk_mean},
                   {"jerk_px_s3_p95", jerk_p95}};
  j["peak_rss_mb"] = peak_rss_mb;
  nlohmann::json reps = nlohmann::json::array();
  for (const auto& r : replays) {
    reps.push_back({{"id", r.id},
                    {"source_start", r.source_start},
                    {"source_end", r.source_end},
                    {"source_frames", r.source_frames},
                    {"mark_to_clip_ms", r.mark_to_clip_ms},
                    {"clip", r.clip_path}});
  }
  j["replays"] = reps;
  return j.dump(2);
}

Pipeline::Pipeline(PipelineConfig cfg) : cfg_(std::move(cfg)) {
  src_ = std::make_unique<MediaSource>(cfg_.input, cfg_.decode_threads);
  fps_ = av_q2d(src_->frame_rate());
  if (src_->pixel_format() != AV_PIX_FMT_YUV420P && src_->pixel_format() != AV_PIX_FMT_YUVJ420P) {
    throw std::runtime_error("only 8-bit YUV 4:2:0 input is supported");
  }
  if (!cfg_.events_file.empty()) {
    events_ = parse_events(read_file(cfg_.events_file));
  }

  // Capacity for per-frame statistics (allocated now, never during the run).
  const double span = cfg_.duration_seconds > 0
                          ? cfg_.duration_seconds
                          : std::max(1.0, src_->duration() - cfg_.start_seconds);
  const auto frames = static_cast<std::size_t>(span * fps_ * 1.5) + 1024;
  work_decode_ = LatencyRecorder(frames);
  work_analysis_ = LatencyRecorder(frames);
  work_composite_ = LatencyRecorder(frames);
  work_encode_ = LatencyRecorder(frames);
  latency_total_ = LatencyRecorder(frames);
  cam_x_.reserve(frames);
  cam_w_.reserve(frames);
  ingest_by_index_.assign(kIngestRing, 0);

  for (std::size_t i = 0; i < kFramePool; ++i) {
    frame_pool_.push_back(make_frame());
    AVFrame* f = frame_pool_.back().get();
    frame_return_.push(std::move(f));
  }
  const int ow = cfg_.compositor.out_width;
  const int oh = cfg_.compositor.out_height;
  for (std::size_t i = 0; i < kOutPool; ++i) {
    out_pool_.push_back(make_nv12_frame(ow, oh));
    AVFrame* f = out_pool_.back().get();
    out_return_.push(std::move(f));
  }
  for (std::size_t i = 0; i < kAudioPool; ++i) {
    audio_pool_.push_back(make_packet());
    AVPacket* p = audio_pool_.back().get();
    audio_return_.push(std::move(p));
  }
  status_ = "{}";
}

Pipeline::~Pipeline() = default;

void Pipeline::request_stop() { stop_.store(true, std::memory_order_release); }

void Pipeline::submit(Command c) {
  std::lock_guard lock(inbox_mu_);
  if (inbox_.size() < 64) {
    inbox_.push_back(std::move(c));
  }
}

std::string Pipeline::status_json() const {
  std::lock_guard lock(status_mu_);
  return status_;
}

void Pipeline::fail(const std::exception& e) {
  {
    std::lock_guard lock(error_mu_);
    if (error_.empty()) {
      error_ = e.what();
    }
  }
  log_error(std::string("pipeline stage failed: ") + e.what());
  stop_.store(true, std::memory_order_release);
  close_all();
}

void Pipeline::close_all() {
  decoded_.close();
  analysed_.close();
  rendered_.close();
  commands_.close();
  frame_return_.close();
  out_return_.close();
  audio_.close();
  audio_return_.close();
}

RunSummary Pipeline::run() {
  const std::int64_t t0 = now_ns();
  wall_start_ns_ = t0;
  {
    std::vector<std::thread> threads;
    threads.emplace_back([this] { decode_thread(); });
    threads.emplace_back([this] { analysis_thread(); });
    threads.emplace_back([this] { compositor_thread(); });
    threads.emplace_back([this] { encoder_thread(); });
    for (auto& t : threads) {
      t.join();
    }
  }
  if (!error_.empty()) {
    throw std::runtime_error(error_);
  }

  RunSummary s;
  s.renderer = renderer_;
  s.source_size = src_->size();
  s.fps = fps_;
  s.wall_seconds = static_cast<double>(now_ns() - t0) / 1e9;
  s.live_frames = live_frames_;
  s.source_seconds = static_cast<double>(live_frames_) / fps_;
  s.replay_source_frames = replay_source_frames_;
  s.output_frames = output_frames_;
  s.audio_packets = audio_packets_;
  s.audio_dropped = audio_dropped_;
  s.output_bytes = output_bytes_;
  s.late_frames = late_frames_;
  s.encoder_held_frames = encoder_held_;
  s.latency_total = latency_total_.summarize();
  s.work_decode = work_decode_.summarize();
  s.work_analysis = work_analysis_.summarize();
  s.work_composite = work_composite_.summarize();
  s.work_encode = work_encode_.summarize();
  if (live_frames_ > 0) {
    s.players_mean = players_sum_ / static_cast<double>(live_frames_);
  }
  s.in_view_rate = players_sum_ > 0 ? in_view_sum_ / players_sum_ : 0.0;

  // Camera motion from the live virtual-camera path (centre x per frame).
  std::vector<double> speed;
  std::vector<double> jerk;
  for (std::size_t i = 1; i < cam_x_.size(); ++i) {
    speed.push_back(std::abs(cam_x_[i] - cam_x_[i - 1]) * fps_);
  }
  for (std::size_t i = 3; i < cam_x_.size(); ++i) {
    // Third finite difference: x[i] - 3x[i-1] + 3x[i-2] - x[i-3], scaled by fps^3.
    const double d3 = cam_x_[i] - 3 * cam_x_[i - 1] + 3 * cam_x_[i - 2] - cam_x_[i - 3];
    jerk.push_back(std::abs(d3) * fps_ * fps_ * fps_);
  }
  if (!speed.empty()) {
    double sum = 0;
    for (double v : speed) sum += v;
    s.pan_speed_mean = sum / static_cast<double>(speed.size());
    s.pan_speed_p95 = percentile_of(speed, 95);
    s.pan_speed_max = speed.back();  // sorted by percentile_of
  }
  if (!jerk.empty()) {
    double sum = 0;
    for (double v : jerk) sum += v;
    s.jerk_mean = sum / static_cast<double>(jerk.size());
    s.jerk_p95 = percentile_of(jerk, 95);
  }
  s.peak_rss_mb = peak_rss_mb();
  s.replays = replay_meta_;
  for (auto& r : s.replays) {
    for (const auto& [id, ms] : clip_ready_ms_) {
      if (id == r.id) r.mark_to_clip_ms = ms;
    }
    const auto idx = static_cast<std::size_t>(r.id);
    if (idx < clip_paths_.size()) r.clip_path = clip_paths_[idx];
  }
  return s;
}

// ============================================================================
// Decode thread: demux, decode, replay insertion, timeline and command order.
// ============================================================================

AVFrame* Pipeline::take_frame_shell() {
  AVFrame* f = nullptr;
  if (!frame_return_.pop(f)) {
    throw std::runtime_error("frame pool closed");
  }
  return f;
}

void Pipeline::handle_command(Command c, double live_time) {
  if (c.type == CommandType::Replay) {
    const double len = c.seconds > 0 ? c.seconds : 8.0;
    scheduled_.push_back({live_time, live_time - len, live_time, now_ns()});
    return;
  }
  if (c.type == CommandType::Goal) {
    const auto& r = cfg_.replay;
    const double pre = c.seconds > 0 ? c.seconds : r.goal_pre_seconds;
    scheduled_.push_back({live_time + r.goal_delay_seconds, live_time - pre,
                          live_time + r.goal_post_seconds, now_ns()});
  }
  c.seq = ++cmd_seq_;
  commands_.push(std::move(c));
}

void Pipeline::emit_live(AVFrame* decoded, std::int64_t ingest_ns) {
  AVFrame* shell = take_frame_shell();
  av_frame_move_ref(shell, decoded);
  FrameMsg m;
  m.frame = shell;
  m.source_index = src_->frame_index(shell->pts);
  m.source_time = src_->seconds(shell->pts);
  m.out_index = out_index_++;
  m.cmd_seq = cmd_seq_;
  if (fade_in_left_ > 0) {
    m.fade = 1.f - static_cast<float>(fade_in_left_) / static_cast<float>(kFadeFrames + 1);
    --fade_in_left_;
  }
  m.t.ingest_ns = ingest_ns;
  m.t.decoded_ns = now_ns();
  live_time_ = m.source_time;
  ++live_frames_;
  decoded_.push(std::move(m));
}

void Pipeline::run_replay(double start_s, double end_s, std::int64_t mark_ns) {
  // Separate decoder so the live decoder's reference frames are untouched.
  if (!replay_dec_) {
    replay_dec_ = src_->make_decoder(0);
  }
  AVCodecContext* dec = replay_dec_.get();
  avcodec_flush_buffers(dec);

  const AVRational tb = src_->video_time_base();
  const auto to_pts = [&](double s) {
    return static_cast<std::int64_t>(std::llround(s / av_q2d(tb)));
  };
  const std::int64_t start_pts = to_pts(std::max(start_s, cfg_.start_seconds));
  const std::int64_t end_pts = to_pts(end_s);

  PacketRing& ring = *ring_;
  std::int64_t k = ring.keyframe_at_or_before(start_pts);
  if (k < 0) {
    k = ring.keyframe_at_or_before(ring.newest_pts());
    if (k < 0 || ring.size() == 0) {
      log_warn("replay skipped: nothing buffered");
      return;
    }
  }

  const int id = next_replay_id_++;
  const int slow = std::max(1, cfg_.replay.slow_factor);
  const double total = std::max(1.0, (end_s - start_s) * fps_);
  int emitted = 0;
  AvFramePtr frame = make_frame();

  auto emit = [&](AVFrame* f) {
    AVFrame* shell = take_frame_shell();
    av_frame_move_ref(shell, f);
    FrameMsg m;
    m.frame = shell;
    m.source_index = src_->frame_index(shell->pts);
    m.source_time = src_->seconds(shell->pts);
    m.out_index = out_index_;
    out_index_ += slow;
    m.repeat = slow;
    m.replay_id = id;
    m.replay_progress = static_cast<float>(emitted / total);
    const int from_start = emitted;
    const int to_end = static_cast<int>(total) - 1 - emitted;
    const int edge = std::min(from_start, to_end);
    m.fade = edge < kFadeFrames ? static_cast<float>(edge + 1) / (kFadeFrames + 1) : 1.f;
    m.cmd_seq = cmd_seq_;
    m.mark_ns = mark_ns;
    m.t.ingest_ns = now_ns();
    m.t.decoded_ns = m.t.ingest_ns;
    ++emitted;
    decoded_.push(std::move(m));
  };

  bool done = false;
  for (auto i = static_cast<std::size_t>(k); i < ring.size() && !done; ++i) {
    const AVPacket* p = ring.at(i);
    av_check(avcodec_send_packet(dec, p), "replay decode");
    while (avcodec_receive_frame(dec, frame.get()) == 0) {
      if (frame->pts < start_pts) {
        av_frame_unref(frame.get());  // decoded only as a reference for later frames
        continue;
      }
      if (frame->pts > end_pts) {
        av_frame_unref(frame.get());
        done = true;
        break;
      }
      emit(frame.get());
    }
  }
  if (!done) {
    avcodec_send_packet(dec, nullptr);  // drain the decoder's delay
    while (avcodec_receive_frame(dec, frame.get()) == 0) {
      if (frame->pts >= start_pts && frame->pts <= end_pts) {
        emit(frame.get());
      } else {
        av_frame_unref(frame.get());
      }
    }
  }

  replay_source_frames_ += emitted;
  inserted_seconds_ += emitted * slow / fps_;
  fade_in_left_ = kFadeFrames;
  replay_meta_.push_back({id, start_s, end_s, emitted, 0.0, {}});
  char msg[128];
  std::snprintf(msg, sizeof(msg), "replay #%d: source %.2fs..%.2fs, %d frames at 1/%d speed", id,
                start_s, end_s, emitted, slow);
  log_info(msg);
}

void Pipeline::decode_thread() {
  try {
    const auto ring_packets =
        static_cast<std::size_t>(std::max(2.0, cfg_.replay.ring_seconds * fps_ * 1.2));
    ring_ = std::make_unique<PacketRing>(ring_packets);

    if (cfg_.start_seconds > 0) {
      src_->seek(cfg_.start_seconds);
    }
    const double end_time =
        cfg_.duration_seconds > 0 ? cfg_.start_seconds + cfg_.duration_seconds : 1e18;
    const AVStream* astream = src_->audio_stream();
    const AVRational atb = astream != nullptr ? astream->time_base : AVRational{1, 1};
    const AVRational vtb = src_->video_time_base();
    const std::int64_t start_pts =
        static_cast<std::int64_t>(std::llround(cfg_.start_seconds / av_q2d(vtb)));

    AvPacketPtr pkt = make_packet();
    AvFramePtr frame = make_frame();
    bool eof = false;
    std::int64_t next_slot = 0;  // realtime pacing: output frame slot

    auto drain_decoder = [&]() {
      for (;;) {
        const int r = src_->receive(frame.get());
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
          return;
        }
        av_check(r, "decode");
        if (frame->pts < start_pts) {
          av_frame_unref(frame.get());  // before the requested start (after seek)
          continue;
        }
        const std::int64_t idx = src_->frame_index(frame->pts);
        const std::int64_t ingest = ingest_by_index_[static_cast<std::size_t>(idx) % kIngestRing];
        emit_live(frame.get(), ingest != 0 ? ingest : now_ns());
      }
    };

    while (!stop_.load(std::memory_order_acquire) && !eof && live_time_ < end_time) {
      // Operator commands (HTTP) and scripted events, in timeline order.
      {
        std::deque<Command> pending;
        {
          std::lock_guard lock(inbox_mu_);
          pending.swap(inbox_);
        }
        for (Command& c : pending) {
          handle_command(std::move(c), live_time_);
        }
      }
      while (next_event_ < events_.size() && events_[next_event_].t <= live_time_) {
        Command c = events_[next_event_++];
        handle_command(std::move(c), live_time_);
      }
      for (std::size_t i = 0; i < scheduled_.size();) {
        if (scheduled_[i].trigger_time <= live_time_) {
          const ScheduledReplay r = scheduled_[i];
          scheduled_.erase(scheduled_.begin() + static_cast<std::ptrdiff_t>(i));
          run_replay(r.start, r.end, r.mark_ns);
        } else {
          ++i;
        }
      }

      const std::int64_t t_read = now_ns();
      if (cfg_.realtime) {
        // Simulate a live camera: frame n becomes available at start + n / fps.
        // Output slots advance with replays too, like a broadcast timeline.
        next_slot = std::max(next_slot, out_index_);
        const auto due =
            wall_start_ns_ + static_cast<std::int64_t>(static_cast<double>(next_slot) * 1e9 / fps_);
        if (t_read < due) {
          std::this_thread::sleep_for(std::chrono::nanoseconds(due - t_read));
        } else if (t_read - due > static_cast<std::int64_t>(1e9 / fps_)) {
          ++late_frames_;
        }
      }

      const MediaSource::Kind kind = src_->read(pkt.get());
      if (kind == MediaSource::Kind::End) {
        eof = true;
        break;
      }
      if (kind == MediaSource::Kind::Audio) {
        if (cfg_.audio && astream != nullptr && pkt->pts != AV_NOPTS_VALUE) {
          const double t = static_cast<double>(pkt->pts) * av_q2d(atb);
          AVPacket* shell = nullptr;
          if (t >= cfg_.start_seconds && t < end_time && audio_return_.try_pop(shell)) {
            av_packet_move_ref(shell, pkt.get());
            // Re-time onto the output timeline: replays push live content later.
            const double out_t = t - cfg_.start_seconds + inserted_seconds_;
            const auto out_pts = static_cast<std::int64_t>(std::llround(out_t / av_q2d(atb)));
            shell->dts = out_pts + (shell->dts - shell->pts);
            shell->pts = out_pts;
            if (!audio_.try_push(std::move(shell))) {
              ++audio_dropped_;  // never block the video path on audio
            }
          } else if (t >= cfg_.start_seconds && t < end_time) {
            ++audio_dropped_;  // pool exhausted (packets past the end aren't drops)
          }
        }
        av_packet_unref(pkt.get());
        continue;
      }
      if (kind != MediaSource::Kind::Video) {
        av_packet_unref(pkt.get());
        continue;
      }

      const std::int64_t t0 = now_ns();
      if (pkt->pts != AV_NOPTS_VALUE) {
        const std::int64_t idx = src_->frame_index(pkt->pts);
        ingest_by_index_[static_cast<std::size_t>(idx) % kIngestRing] = t0;
      }
      ring_->push(pkt.get());
      const int r = src_->send(pkt.get());
      av_packet_unref(pkt.get());
      if (r < 0 && r != AVERROR(EAGAIN)) {
        av_check(r, "send packet");
      }
      drain_decoder();
      ++next_slot;
      work_decode_.record(now_ns() - t0);
    }
    if (eof) {
      src_->send(nullptr);
      drain_decoder();
    }
  } catch (const std::exception& e) {
    fail(e);
  }
  decoded_.close();
  audio_.close();
  commands_.close();
}

// ============================================================================
// Analysis thread: detect -> track -> direct.
// ============================================================================

void Pipeline::analysis_thread() {
  try {
    const Size size = src_->size();
    PitchMask mask(cfg_.pitch_polygon, size.width / cfg_.detector.downscale,
                   size.height / cfg_.detector.downscale);
    const RectF nb = mask.bounds_normalised();
    const RectF pitch_px{nb.x * size.width, nb.y * size.height, nb.width * size.width,
                         nb.height * size.height};
    BackgroundDetector detector(size.width, size.height, std::move(mask), cfg_.detector);
    Tracker tracker(cfg_.tracker);
    Director director(size, pitch_px, cfg_.director);
    const double dt = 1.0 / fps_;

    struct CamRecord {
      std::int64_t index{-1};
      RectF crop{};
    };
    std::vector<CamRecord> history(2048);
    RectF last_crop = director.last().crop;

    FrameMsg m;
    while (decoded_.pop(m)) {
      const std::int64_t t0 = now_ns();
      if (m.replay_id < 0) {
        const AVFrame* f = m.frame;
        const GrayView luma{f->data[0], f->width, f->height, f->linesize[0]};
        const auto& dets = detector.detect(luma);
        tracker.update(dets, dt);
        const DirectorOutput& d = director.update(tracker.tracks(), dt);
        m.crop = d.crop;
        m.players = d.players;
        int in_view = 0;
        for (const Track& t : tracker.tracks()) {
          if (!t.confirmed || t.misses > 0) continue;
          const Vec2 p = t.position();
          if (p.x >= d.crop.x && p.x <= d.crop.x + d.crop.width && p.y >= d.crop.y &&
              p.y <= d.crop.y + d.crop.height) {
            ++in_view;
          }
        }
        m.players_in_view = in_view;
        history[static_cast<std::size_t>(m.source_index) % history.size()] = {m.source_index,
                                                                              d.crop};
        if (!cfg_.debug_dir.empty() && m.source_index % 250 == 0) {
          std::filesystem::create_directories(cfg_.debug_dir);
          const std::string base = cfg_.debug_dir + "/" + std::to_string(m.source_index);
          write_pgm(base + "_frame.pgm", detector.small_frame());
          write_pgm(base + "_fg.pgm", detector.foreground());
          // Detection boxes drawn over the analysis frame, for eyeballing.
          GrayImage boxes = detector.small_frame();
          const double ds = cfg_.detector.downscale;
          for (const Detection& det : dets) {
            const int x0 = static_cast<int>(det.box.x / ds);
            const int y0 = static_cast<int>(det.box.y / ds);
            const int x1 =
                std::min(boxes.width - 1, static_cast<int>((det.box.x + det.box.width) / ds));
            const int y1 =
                std::min(boxes.height - 1, static_cast<int>((det.box.y + det.box.height) / ds));
            for (int x = x0; x <= x1; ++x) {
              boxes.row(y0)[x] = 255;
              boxes.row(y1)[x] = 255;
            }
            for (int y = y0; y <= y1; ++y) {
              boxes.row(y)[x0] = 255;
              boxes.row(y)[x1] = 255;
            }
          }
          write_pgm(base + "_boxes.pgm", boxes);
        }
        last_crop = d.crop;
      } else {
        // Replays reuse the camera path recorded live, a little tighter.
        const CamRecord& rec = history[static_cast<std::size_t>(m.source_index) % history.size()];
        const RectF base = rec.index == m.source_index ? rec.crop : last_crop;
        const Vec2 c{base.x + base.width / 2.0, base.y + base.height / 2.0};
        m.crop = compute_crop(size, c, base.width * cfg_.replay.zoom,
                              {cfg_.director.aspect, cfg_.director.min_width_px * 0.7});
      }
      m.t.inferred_ns = now_ns();
      work_analysis_.record(m.t.inferred_ns - t0);
      if (!analysed_.push(std::move(m))) {
        break;
      }
    }
  } catch (const std::exception& e) {
    fail(e);
  }
  analysed_.close();
}

// ============================================================================
// Compositor thread: GPU crop + graphics + NV12 conversion.
// ============================================================================

void Pipeline::compositor_thread() {
  try {
    const Size size = src_->size();
    Compositor comp(size.width, size.height, cfg_.compositor);
    renderer_ = comp.renderer();
    char line[256];
    std::snprintf(line, sizeof(line),
                  "compositor: %s (%dx%d -> %dx%d, crop-only upload %d, async readback %d)",
                  comp.renderer().c_str(), size.width, size.height, cfg_.compositor.out_width,
                  cfg_.compositor.out_height, cfg_.compositor.upload_crop_only ? 1 : 0,
                  cfg_.compositor.async_readback ? 1 : 0);
    log_info(line);
    ScoreboardState board;
    OverlayBatch batch;
    std::uint32_t applied_seq = 0;
    AVFrame* spare = nullptr;
    FrameMsg prev;
    bool has_prev = false;

    auto publish = [&](const FrameMsg& meta, AVFrame* out) {
      OutMsg o;
      o.frame = out;
      o.meta = meta;
      o.meta.frame = nullptr;
      o.meta.t.composited_ns = now_ns();
      rendered_.push(std::move(o));
    };

    FrameMsg m;
    while (analysed_.pop(m)) {
      const std::int64_t t0 = now_ns();
      const double out_time = static_cast<double>(m.out_index) / fps_;
      while (applied_seq < m.cmd_seq) {
        Command c;
        if (!commands_.pop(c)) break;
        board.apply(c, out_time, m.source_time);
        applied_seq = c.seq;
      }
      OverlayContext ctx;
      ctx.width = cfg_.compositor.out_width;
      ctx.height = cfg_.compositor.out_height;
      ctx.out_time = out_time;
      ctx.source_time = m.source_time;
      ctx.replay = m.replay_id >= 0;
      ctx.replay_progress = m.replay_progress;
      build_overlay(batch, comp.atlas(), board, ctx);

      if (spare == nullptr && !out_return_.pop(spare)) {
        break;
      }
      const bool produced = comp.render(m.frame, m.crop, m.fade, batch, spare);
      // The texture upload has copied the pixels: hand the decoded picture back.
      av_frame_unref(m.frame);
      AVFrame* shell = m.frame;
      frame_return_.push(std::move(shell));
      m.frame = nullptr;

      if (cfg_.compositor.async_readback) {
        if (produced && has_prev) {
          publish(prev, spare);
          spare = nullptr;
        }
        prev = m;
        has_prev = true;
      } else {
        publish(m, spare);
        spare = nullptr;
      }
      work_composite_.record(now_ns() - t0);

      if (m.out_index % 25 == 0) {
        nlohmann::json st = nlohmann::json::parse(board.to_json());
        st["source_time"] = m.source_time;
        st["out_index"] = m.out_index;
        st["replay"] = m.replay_id >= 0;
        st["players_tracked"] = m.players;
        std::lock_guard lock(status_mu_);
        status_ = st.dump();
      }
    }
    if (has_prev) {
      if (spare == nullptr) {
        out_return_.pop(spare);
      }
      if (spare != nullptr && comp.flush(spare)) {
        publish(prev, spare);
        spare = nullptr;
      }
    }
  } catch (const std::exception& e) {
    fail(e);
  }
  rendered_.close();
}

// ============================================================================
// Encoder thread: x264, mux, audio passthrough, replay clips, stats.
// ============================================================================

void Pipeline::encoder_thread() {
  try {
    OutputOptions oo;
    oo.url = cfg_.output;
    oo.width = cfg_.compositor.out_width;
    oo.height = cfg_.compositor.out_height;
    oo.fps = src_->frame_rate();
    oo.preset = cfg_.x264_preset;
    oo.crf = cfg_.crf;
    oo.bitrate_kbps = cfg_.bitrate_kbps;
    oo.zerolatency = cfg_.realtime;
    oo.audio_source = cfg_.audio ? src_->audio_stream() : nullptr;
    ProgramOutput program(oo);

    std::unique_ptr<ProgramOutput> clip;
    int clip_id = -1;
    std::int64_t clip_index = 0;
    std::int64_t clip_mark_ns = 0;
    auto close_clip = [&]() {
      if (!clip) return;
      clip->finish();
      clip.reset();
      clip_ready_ms_.emplace_back(clip_id, static_cast<double>(now_ns() - clip_mark_ns) / 1e6);
      clip_id = -1;
    };

    std::FILE* camlog = nullptr;
    if (!cfg_.camera_log.empty()) {
      camlog = std::fopen(cfg_.camera_log.c_str(), "w");
      if (camlog != nullptr) {
        std::fputs(
            "out_index,source_time,replay_id,crop_x,crop_y,crop_w,crop_h,players,in_view,"
            "latency_ms\n",
            camlog);
      }
    }

    auto drain_audio = [&]() {
      AVPacket* p = nullptr;
      while (audio_.try_pop(p)) {
        program.write_audio(p);
        ++audio_packets_;
        av_packet_unref(p);
        audio_return_.push(std::move(p));
      }
    };

    OutMsg o;
    while (rendered_.pop(o)) {
      const std::int64_t t0 = now_ns();
      drain_audio();
      const FrameMsg& m = o.meta;
      for (int k = 0; k < m.repeat; ++k) {
        program.write_video(o.frame, m.out_index + k);
      }
      ++output_frames_;
      output_frames_ += m.repeat - 1;

      if (m.replay_id >= 0 && cfg_.replay.export_clips) {
        if (m.replay_id != clip_id) {
          close_clip();
          std::filesystem::create_directories(cfg_.replay.clip_dir);
          char name[64];
          std::snprintf(name, sizeof(name), "/replay_%03d.mp4", m.replay_id);
          OutputOptions co = oo;
          co.url = cfg_.replay.clip_dir + name;
          co.audio_source = nullptr;
          co.zerolatency = false;
          clip = std::make_unique<ProgramOutput>(co);
          clip_id = m.replay_id;
          clip_index = 0;
          clip_mark_ns = m.mark_ns;
          if (static_cast<std::size_t>(clip_id) >= clip_paths_.size()) {
            clip_paths_.resize(static_cast<std::size_t>(clip_id) + 1);
          }
          clip_paths_[static_cast<std::size_t>(clip_id)] = co.url;
        }
        for (int k = 0; k < m.repeat; ++k) {
          clip->write_video(o.frame, clip_index++);
        }
      } else if (clip) {
        close_clip();
      }

      const std::int64_t done = now_ns();
      work_encode_.record(done - t0);
      double latency_ms = 0.0;
      if (m.replay_id < 0) {
        // ingest -> handed to the muxer. With x264's lookahead (file mode) the
        // packet itself may leave a few frames later; realtime mode uses
        // tune=zerolatency so the two coincide.
        latency_total_.record(done - m.t.ingest_ns);
        latency_ms = static_cast<double>(done - m.t.ingest_ns) / 1e6;
        players_sum_ += m.players;
        in_view_sum_ += m.players_in_view;
        cam_x_.push_back(m.crop.x + m.crop.width / 2.0);
        cam_w_.push_back(m.crop.width);
      }
      if (camlog != nullptr) {
        std::fprintf(camlog, "%lld,%.3f,%d,%.1f,%.1f,%.1f,%.1f,%d,%d,%.2f\n",
                     static_cast<long long>(m.out_index), m.source_time, m.replay_id, m.crop.x,
                     m.crop.y, m.crop.width, m.crop.height, m.players, m.players_in_view,
                     latency_ms);
      }

      if (av_frame_is_writable(o.frame) == 0) {
        ++encoder_held_;  // encoder still holds a reference: copy-on-write next time
        av_check(av_frame_make_writable(o.frame), "make writable");
      }
      AVFrame* f = o.frame;
      out_return_.push(std::move(f));
    }
    drain_audio();
    close_clip();
    program.finish();
    output_bytes_ = program.bytes_written();
    if (camlog != nullptr) {
      std::fclose(camlog);
    }
  } catch (const std::exception& e) {
    fail(e);
  }
}

}  // namespace md
