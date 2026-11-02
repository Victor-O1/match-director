find_package(GTest REQUIRED)

# Pure logic: runs under ASAN/UBSAN and TSAN everywhere.
add_executable(md_tests
  spsc_queue_test.cpp
  latency_stats_test.cpp
  crop_test.cpp
  tracking_test.cpp
  director_test.cpp
  detector_test.cpp
  control_test.cpp)
target_link_libraries(md_tests PRIVATE md_core GTest::gtest_main)
md_set_warnings(md_tests)

# Needs FFmpeg/FreeType (no GPU).
add_executable(md_media_tests media_test.cpp)
target_link_libraries(md_media_tests PRIVATE md_media GTest::gtest_main)
md_set_warnings(md_media_tests)

include(GoogleTest)
# PRE_TEST: list tests when ctest runs, not right after linking. A TSAN binary
# run at build time aborts under WSL's ASLR (see LESSONS.md); at ctest time it
# runs under `setarch -R`.
gtest_discover_tests(md_tests DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(md_media_tests DISCOVERY_MODE PRE_TEST)
