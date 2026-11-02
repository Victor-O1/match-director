find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
pkg_check_modules(GLES REQUIRED IMPORTED_TARGET egl glesv2)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

# Pure C++ logic: no FFmpeg/GL, so it is fast to test and sanitizer-friendly.
add_library(md_core STATIC
  core/latency_stats.cpp
  core/log.cpp
  director/crop.cpp
  director/director.cpp
  vision/image.cpp
  vision/pitch_mask.cpp
  vision/detector.cpp
  track/hungarian.cpp
  track/tracker.cpp
  control/command.cpp
  control/http_server.cpp)
target_include_directories(md_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(md_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads
                      PRIVATE spdlog::spdlog)
md_set_warnings(md_core)

# Media + GPU + pipeline.
add_library(md_media STATIC
  media/source.cpp
  media/output.cpp
  replay/packet_ring.cpp
  gfx/egl_context.cpp
  gfx/glyph_atlas.cpp
  gfx/overlay.cpp
  gfx/compositor.cpp
  pipeline/config.cpp
  pipeline/pipeline.cpp)
target_link_libraries(md_media PUBLIC md_core PkgConfig::FFMPEG PkgConfig::GLES PkgConfig::FREETYPE
                      spdlog::spdlog)
md_set_warnings(md_media)

add_executable(md_probe apps/md_probe.cpp)
target_link_libraries(md_probe PRIVATE md_core PkgConfig::FFMPEG)
md_set_warnings(md_probe)

add_executable(match_director apps/match_director.cpp)
target_link_libraries(match_director PRIVATE md_media)
md_set_warnings(match_director)
