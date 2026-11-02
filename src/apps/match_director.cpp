// match_director: turns a fixed wide-angle match recording (or live feed) into
// a produced broadcast: auto-directed virtual camera, scoreboard graphics,
// instant replays, H.264 output.
//
//   match_director --input match.mp4 --output program.mp4 --config config/your_match.json
//                  [--start 600 --duration 120] [--events events.json] [--http 8080]

#include "md/control/http_server.hpp"
#include "md/control/panel_html.hpp"
#include "md/pipeline/config.hpp"
#include "md/pipeline/pipeline.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_interrupted = 0;

void on_signal(int /*sig*/) { g_interrupted = 1; }

void usage() {
  std::fputs(
      "usage: match_director --input <file|url> [options]\n"
      "  --output <file|rtmp://|srt://>   program output (default program.mp4)\n"
      "  --config <json>                  pitch polygon + tuning (see config/)\n"
      "  --events <json>                  timed operator events (goals, captions, replays)\n"
      "  --start <s> --duration <s>       process part of the input\n"
      "  --realtime                       pace input at its frame rate (live simulation)\n"
      "  --http <port> [--http-bind ip]   operator control panel + REST API\n"
      "  --size <WxH>                     output size (default 1280x720)\n"
      "  --gpu <auto|d3d12|llvmpipe>      GL driver\n"
      "  --full-upload --sync-readback --no-supersample   compositor A/B switches\n"
      "  --preset <x264 preset> --crf <n> --bitrate <kbps>\n"
      "  --no-audio --no-clips --clip-dir <dir> --decode-threads <n>\n"
      "  --stats <json> --camera-log <csv> --debug-dir <dir>\n",
      stderr);
}

}  // namespace

int main(int argc, char** argv) {
  md::PipelineConfig cfg;
  std::string config_path;
  bool decode_threads_given = false;
  try {
    // Config file first, so command-line flags override it regardless of order.
    for (int i = 1; i + 1 < argc; ++i) {
      if (std::string(argv[i]) == "--config") config_path = argv[i + 1];
    }
    if (!config_path.empty()) {
      md::load_config_file(config_path, cfg);
    }
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      auto next = [&]() -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
        return argv[++i];
      };
      if (a == "--input")
        cfg.input = next();
      else if (a == "--output")
        cfg.output = next();
      else if (a == "--config")
        next();
      else if (a == "--events")
        cfg.events_file = next();
      else if (a == "--start")
        cfg.start_seconds = std::stod(next());
      else if (a == "--duration")
        cfg.duration_seconds = std::stod(next());
      else if (a == "--realtime")
        cfg.realtime = true;
      else if (a == "--http")
        cfg.http_port = std::stoi(next());
      else if (a == "--http-bind")
        cfg.http_bind = next();
      else if (a == "--gpu")
        cfg.compositor.driver = next();
      else if (a == "--full-upload")
        cfg.compositor.upload_crop_only = false;
      else if (a == "--sync-readback")
        cfg.compositor.async_readback = false;
      else if (a == "--no-supersample")
        cfg.compositor.supersample = false;
      else if (a == "--preset")
        cfg.x264_preset = next();
      else if (a == "--crf")
        cfg.crf = std::stoi(next());
      else if (a == "--bitrate")
        cfg.bitrate_kbps = std::stoi(next());
      else if (a == "--no-audio")
        cfg.audio = false;
      else if (a == "--no-clips")
        cfg.replay.export_clips = false;
      else if (a == "--clip-dir")
        cfg.replay.clip_dir = next();
      else if (a == "--decode-threads") {
        cfg.decode_threads = std::stoi(next());
        decode_threads_given = true;
      } else if (a == "--stats")
        cfg.stats_file = next();
      else if (a == "--camera-log")
        cfg.camera_log = next();
      else if (a == "--debug-dir")
        cfg.debug_dir = next();
      else if (a == "--size") {
        const std::string v = next();
        const auto x = v.find('x');
        if (x == std::string::npos) throw std::invalid_argument("--size expects WxH");
        cfg.compositor.out_width = std::stoi(v.substr(0, x));
        cfg.compositor.out_height = std::stoi(v.substr(x + 1));
      } else if (a == "--help" || a == "-h") {
        usage();
        return 0;
      } else {
        throw std::invalid_argument("unknown option " + a);
      }
    }
    if (cfg.input.empty()) {
      usage();
      return 2;
    }
    // FFmpeg frame threading delays output by ~1 frame per thread. Measured on
    // the 4K test match: auto (~20 threads) = 666 ms p50 end-to-end latency,
    // 4 threads = 185 ms with no late frames. Live mode favours latency.
    if (cfg.realtime && !decode_threads_given) {
      cfg.decode_threads = 4;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    usage();
    return 2;
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  try {
    md::Pipeline pipeline(cfg);

    std::unique_ptr<md::HttpServer> http;
    if (cfg.http_port > 0) {
      http = std::make_unique<md::HttpServer>(
          cfg.http_bind, cfg.http_port, [&pipeline](const md::HttpRequest& req) {
            if (req.method == "GET" && req.path == "/") {
              return md::HttpResponse{200, "text/html; charset=utf-8", md::kPanelHtml};
            }
            if (req.method == "GET" && req.path == "/api/state") {
              return md::HttpResponse{200, "application/json", pipeline.status_json()};
            }
            if (req.path == "/api/command") {
              if (req.method != "POST") {
                return md::HttpResponse{405, "application/json", R"({"error":"use POST"})"};
              }
              try {
                pipeline.submit(md::parse_command(req.body));
                return md::HttpResponse{200, "application/json", R"({"ok":true})"};
              } catch (const std::exception& e) {
                nlohmann::json err = {{"error", e.what()}};
                return md::HttpResponse{400, "application/json", err.dump()};
              }
            }
            return md::HttpResponse{404, "application/json", R"({"error":"not found"})"};
          });
      spdlog::info("control panel: http://{}:{}/", cfg.http_bind, http->port());
    }

    std::atomic<bool> finished{false};
    std::thread watcher([&] {
      while (!finished.load()) {
        if (g_interrupted != 0) {
          spdlog::warn("interrupted: draining pipeline");
          pipeline.request_stop();
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });

    spdlog::info("processing {} -> {}", cfg.input, cfg.output);
    md::RunSummary summary;
    try {
      summary = pipeline.run();
    } catch (...) {
      finished = true;
      watcher.join();
      throw;
    }
    finished = true;
    watcher.join();

    const std::string json = summary.to_json();
    std::puts(json.c_str());
    if (!cfg.stats_file.empty()) {
      std::ofstream(cfg.stats_file) << json << "\n";
    }
  } catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  }
  return 0;
}
