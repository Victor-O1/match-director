#pragma once

#include "md/control/command.hpp"
#include "md/core/channel.hpp"
#include "md/core/clock.hpp"
#include "md/core/latency_stats.hpp"
#include "md/director/crop.hpp"
#include "md/media/av_ptr.hpp"
#include "md/pipeline/config.hpp"
#include "md/replay/packet_ring.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace md {

class MediaSource;

// Message from decode -> analysis -> compositor. Plain data, cheap to move.
// `frame` is BORROWED from the pipeline's frame pool; the compositor returns
// it to the decode thread through a channel when done.
struct FrameMsg {
  AVFrame* frame{nullptr};
  std::int64_t source_index{0};
  double source_time{0.0};
  std::int64_t out_index{0};  // first output frame index
  int repeat{1};              // how many output frames (slow motion)
  int replay_id{-1};          // >= 0: part of replay #id
  float replay_progress{0.f};
  float fade{1.f};           // video brightness (dip-to-black transitions)
  std::uint32_t cmd_seq{0};  // apply commands up to this sequence number first
  std::int64_t mark_ns{0};   // replay trigger time (for mark -> clip latency)
  RectF crop{};
  int players{0};
  int players_in_view{0};
  StageTimes t{};
};

// Message from compositor -> encoder: a finished NV12 picture plus metadata.
struct OutMsg {
  AVFrame* frame{nullptr};  // borrowed from the output pool
  FrameMsg meta{};          // `meta.frame` is null here (already released)
};

struct ReplaySummary {
  int id{0};
  double source_start{0.0};
  double source_end{0.0};
  int source_frames{0};
  double mark_to_clip_ms{0.0};
  std::string clip_path;
};

struct RunSummary {
  std::string renderer;
  Size source_size;
  double fps{0.0};
  double source_seconds{0.0};
  double wall_seconds{0.0};
  std::int64_t live_frames{0};
  std::int64_t replay_source_frames{0};
  std::int64_t output_frames{0};
  std::int64_t audio_packets{0};
  std::int64_t audio_dropped{0};
  std::int64_t output_bytes{0};
  std::int64_t late_frames{0};          // realtime mode: frames that missed their slot
  std::int64_t encoder_held_frames{0};  // NV12 buffers still referenced after encode (should be 0)
  LatencySummary latency_total;         // ingest -> packet written (live frames)
  LatencySummary work_decode;           // per frame, thread busy time
  LatencySummary work_analysis;
  LatencySummary work_composite;
  LatencySummary work_encode;
  double players_mean{0.0};
  double in_view_rate{0.0};
  double pan_speed_mean{0.0};  // source px / s
  double pan_speed_p95{0.0};
  double pan_speed_max{0.0};
  double jerk_mean{0.0};  // source px / s^3
  double jerk_p95{0.0};
  double peak_rss_mb{0.0};
  std::vector<ReplaySummary> replays;

  [[nodiscard]] std::string to_json() const;
};

class Pipeline {
 public:
  explicit Pipeline(PipelineConfig cfg);
  ~Pipeline();
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;

  // Runs to completion (or until request_stop()). Throws the first error any
  // stage hit.
  RunSummary run();

  // Thread-safe. Ends the run after in-flight frames are drained.
  void request_stop();

  // Thread-safe: queue an operator command (HTTP thread).
  void submit(Command c);
  // Thread-safe: latest status JSON for the control panel.
  [[nodiscard]] std::string status_json() const;

 private:
  void decode_thread();
  void analysis_thread();
  void compositor_thread();
  void encoder_thread();

  void fail(const std::exception& e);
  void close_all();

  // Decode-thread helpers.
  void handle_command(Command c, double live_time);
  void emit_live(AVFrame* decoded, std::int64_t ingest_ns);
  void run_replay(double start_s, double end_s, std::int64_t mark_ns);
  AVFrame* take_frame_shell();

  PipelineConfig cfg_;
  std::unique_ptr<MediaSource> src_;
  double fps_{25.0};

  // Pools (owned here, lent out through channels).
  std::vector<AvFramePtr> frame_pool_;
  std::vector<AvFramePtr> out_pool_;
  std::vector<AvPacketPtr> audio_pool_;

  Channel<FrameMsg, 16> decoded_;
  Channel<FrameMsg, 16> analysed_;
  Channel<OutMsg, 8> rendered_;
  Channel<Command, 64> commands_;
  Channel<AVFrame*, 32> frame_return_;
  Channel<AVFrame*, 16> out_return_;
  Channel<AVPacket*, 1024> audio_;
  Channel<AVPacket*, 1024> audio_return_;

  // Operator commands from other threads (cold path: a mutex is fine).
  mutable std::mutex inbox_mu_;
  std::deque<Command> inbox_;
  mutable std::mutex status_mu_;
  std::string status_;

  std::atomic<bool> stop_{false};
  std::mutex error_mu_;
  std::string error_;

  // ---- decode-thread state ----
  std::unique_ptr<PacketRing> ring_;
  AvCodecPtr replay_dec_;
  std::vector<Command> events_;
  std::size_t next_event_{0};
  struct ScheduledReplay {
    double trigger_time, start, end;
    std::int64_t mark_ns;
  };
  std::vector<ScheduledReplay> scheduled_;
  std::int64_t out_index_{0};
  std::uint32_t cmd_seq_{0};
  int next_replay_id_{0};
  int fade_in_left_{0};
  double inserted_seconds_{0.0};
  double live_time_{0.0};
  std::int64_t wall_start_ns_{0};
  std::vector<std::int64_t> ingest_by_index_;  // ring: source frame index -> ingest time
  LatencyRecorder work_decode_{1};
  std::int64_t live_frames_{0};
  std::int64_t replay_source_frames_{0};
  std::int64_t audio_dropped_{0};
  std::int64_t late_frames_{0};
  std::vector<ReplaySummary> replay_meta_;  // written by decode thread, read after join

  // ---- per-thread stats, read after join ----
  LatencyRecorder work_analysis_{1};
  LatencyRecorder work_composite_{1};
  LatencyRecorder work_encode_{1};
  LatencyRecorder latency_total_{1};
  std::string renderer_;
  std::int64_t output_frames_{0};
  std::int64_t audio_packets_{0};
  std::int64_t output_bytes_{0};
  std::int64_t encoder_held_{0};
  double players_sum_{0.0};
  double in_view_sum_{0.0};
  std::vector<double> cam_x_, cam_w_;
  std::vector<std::pair<int, double>> clip_ready_ms_;  // replay id -> mark->clip ms
  std::vector<std::string> clip_paths_;
};

}  // namespace md
