add_library(md_core STATIC
  core/latency_stats.cpp
  director/crop.cpp)
target_include_directories(md_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
md_set_warnings(md_core)

# FFmpeg-backed tools. Found via pkg-config (apt packages on Ubuntu).
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

add_executable(md_probe apps/md_probe.cpp)
target_link_libraries(md_probe PRIVATE md_core PkgConfig::FFMPEG)
md_set_warnings(md_probe)
