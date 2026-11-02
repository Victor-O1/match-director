#include "md/pipeline/config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace md {

namespace {

using nlohmann::json;

// Reads known keys from a JSON object into fields and rejects unknown keys.
class Section {
 public:
  Section(const json& j, std::string name) : j_(j), name_(std::move(name)) {
    if (!j_.is_object()) {
      throw std::invalid_argument("config: '" + name_ + "' must be an object");
    }
  }
  // Call after all get()/sub() calls: rejects keys nobody asked for (typos).
  void reject_unknown() const {
    for (const auto& item : j_.items()) {
      if (known_.count(item.key()) == 0) {
        throw std::invalid_argument("config: unknown key '" + name_ + "." + item.key() + "'");
      }
    }
  }

  template <typename T>
  void get(const char* key, T& field) {
    known_.insert(key);
    if (j_.contains(key)) {
      try {
        field = j_.at(key).get<T>();
      } catch (const json::exception&) {
        throw std::invalid_argument("config: bad value for '" + name_ + "." + key + "'");
      }
    }
  }
  [[nodiscard]] const json* sub(const char* key) {
    known_.insert(key);
    return j_.contains(key) ? &j_.at(key) : nullptr;
  }

 private:
  const json& j_;
  std::string name_;
  std::set<std::string> known_;
};

}  // namespace

void load_config_file(const std::string& path, PipelineConfig& cfg) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open config file " + path);
  }
  std::stringstream ss;
  ss << in.rdbuf();
  json root;
  try {
    root = json::parse(ss.str());
  } catch (const json::exception& e) {
    throw std::invalid_argument("config: invalid JSON in " + path + ": " + e.what());
  }

  Section top(root, "root");
  top.get("start_seconds", cfg.start_seconds);
  top.get("duration_seconds", cfg.duration_seconds);
  top.get("audio", cfg.audio);
  top.get("x264_preset", cfg.x264_preset);
  top.get("crf", cfg.crf);
  top.get("bitrate_kbps", cfg.bitrate_kbps);
  top.get("events_file", cfg.events_file);
  top.get("decode_threads", cfg.decode_threads);
  (void)top.sub("comment");

  if (const json* poly = top.sub("pitch_polygon")) {
    cfg.pitch_polygon.clear();
    for (const json& p : *poly) {
      if (!p.is_array() || p.size() != 2) {
        throw std::invalid_argument("config: pitch_polygon entries must be [x, y]");
      }
      cfg.pitch_polygon.push_back({p[0].get<double>(), p[1].get<double>()});
    }
  }
  if (const json* d = top.sub("detector")) {
    Section s(*d, "detector");
    s.get("downscale", cfg.detector.downscale);
    s.get("diff_threshold", cfg.detector.diff_threshold);
    s.get("noise_k", cfg.detector.noise_k);
    s.get("temporal_and", cfg.detector.temporal_and);
    s.get("learn_shift_bg", cfg.detector.learn_shift_bg);
    s.get("learn_shift_fg", cfg.detector.learn_shift_fg);
    s.get("min_area", cfg.detector.min_area);
    s.get("max_area", cfg.detector.max_area);
    s.get("max_width_over_height", cfg.detector.max_width_over_height);
    s.get("min_height", cfg.detector.min_height);
    s.get("vertical_dilate", cfg.detector.vertical_dilate);
    s.get("warmup_frames", cfg.detector.warmup_frames);
    s.reject_unknown();
  }
  if (const json* t = top.sub("tracker")) {
    Section s(*t, "tracker");
    s.get("gate_px", cfg.tracker.gate_px);
    s.get("accel_noise", cfg.tracker.accel_noise);
    s.get("meas_noise", cfg.tracker.meas_noise);
    s.get("confirm_hits", cfg.tracker.confirm_hits);
    s.get("max_misses", cfg.tracker.max_misses);
    s.reject_unknown();
  }
  if (const json* d = top.sub("director")) {
    Section s(*d, "director");
    s.get("cluster_width_frac", cfg.director.cluster_width_frac);
    s.get("speed_ref_px_s", cfg.director.speed_ref_px_s);
    s.get("max_weight", cfg.director.max_weight);
    s.get("lead_seconds", cfg.director.lead_seconds);
    s.get("spread_gain", cfg.director.spread_gain);
    s.get("margin_px", cfg.director.margin_px);
    s.get("min_width_px", cfg.director.min_width_px);
    s.get("max_width_frac", cfg.director.max_width_frac);
    s.get("dead_zone_frac", cfg.director.dead_zone_frac);
    s.get("omega_pan", cfg.director.omega_pan);
    s.get("omega_zoom", cfg.director.omega_zoom);
    s.get("max_pan_px_s", cfg.director.max_pan_px_s);
    s.get("max_zoom_px_s", cfg.director.max_zoom_px_s);
    s.get("vertical_bias", cfg.director.vertical_bias);
    s.reject_unknown();
  }
  if (const json* c = top.sub("compositor")) {
    Section s(*c, "compositor");
    s.get("out_width", cfg.compositor.out_width);
    s.get("out_height", cfg.compositor.out_height);
    s.get("driver", cfg.compositor.driver);
    s.get("font_path", cfg.compositor.font_path);
    s.get("upload_crop_only", cfg.compositor.upload_crop_only);
    s.get("async_readback", cfg.compositor.async_readback);
    s.get("supersample", cfg.compositor.supersample);
    s.reject_unknown();
  }
  if (const json* r = top.sub("replay")) {
    Section s(*r, "replay");
    s.get("ring_seconds", cfg.replay.ring_seconds);
    s.get("goal_pre_seconds", cfg.replay.goal_pre_seconds);
    s.get("goal_post_seconds", cfg.replay.goal_post_seconds);
    s.get("goal_delay_seconds", cfg.replay.goal_delay_seconds);
    s.get("slow_factor", cfg.replay.slow_factor);
    s.get("zoom", cfg.replay.zoom);
    s.get("export_clips", cfg.replay.export_clips);
    s.get("clip_dir", cfg.replay.clip_dir);
    s.reject_unknown();
  }
  top.reject_unknown();
}

}  // namespace md
