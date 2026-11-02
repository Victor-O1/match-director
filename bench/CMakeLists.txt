find_package(benchmark REQUIRED)

add_executable(md_bench
  spsc_bench.cpp
  crop_bench.cpp)
target_link_libraries(md_bench PRIVATE md_core benchmark::benchmark_main)
md_set_warnings(md_bench)
