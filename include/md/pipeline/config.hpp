#pragma once

#include "md/director/crop.hpp"
#include "md/director/director.hpp"
#include "md/gfx/compositor.hpp"
#include "md/track/tracker.hpp"
#include "md/vision/detector.hpp"

#include <string>
#include <vector>

namespace md {

struct ReplayParams {
  double ring_seconds{30.0};       // how far back replays can reach
  double goal_pre_seconds{6.0};    // a goal replay starts this long before the goal event
  double goal_post_seconds{1.0};   // ...and ends this long after it
  double goal_delay_seconds{3.0};  // the replay rolls this long after the goal (celebration)
  int slow_factor{2};              // 2 = half speed
  double zoom{0.72};               // replay crop width relative to the live camera (tighter)
  bool export_clips{true};
  std::string clip_dir{"replays"};
};

struct PipelineConfig {
  // Input / output
  std::string input;
  std::string output{"program.mp4"};
  double start_seconds{0.0};
  double duration_seconds{0.0};  // 0 = until end of input
  bool realtime{false};          // pace input at its frame rate (simulates a live camera)
  bool audio{true};
  int decode_threads{0};  // 0 = auto
  std::string x264_preset{"veryfast"};
  int crf{21};
  int bitrate_kbps{0};

  // Analysis
  std::vector<Vec2> pitch_polygon;  // normalised; empty = whole frame
  DetectorParams detector;
  TrackerParams tracker;
  DirectorParams director;

  // Graphics
  CompositorOptions compositor;

  // Control
  std::string events_file;  // JSON array of timed commands
  int http_port{0};         // 0 = no control server
  std::string http_bind{"127.0.0.1"};

  ReplayParams replay;

  // Diagnostics
  std::string stats_file;  // summary JSON
  std::string camera_log;  // per-frame CSV
  std::string debug_dir;   // if set: dump detector frame/mask PGMs every 250 live frames
};

// Loads overrides from a JSON config file into `cfg` (unknown keys are errors,
// so typos don't silently do nothing).
void load_config_file(const std::string& path, PipelineConfig& cfg);

}  // namespace md
