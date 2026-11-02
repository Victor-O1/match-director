# Match Director

Turns one fixed wide (panoramic) camera at a football match into a finished broadcast. It follows the action with a virtual camera, overlays a live scoreboard, supports instant replay, and streams the result out. Written in C++20.

> **Status: Phase 0 (in progress).** What works today: build system, CI with sanitizer builds, a lock-free SPSC queue for the pipeline, latency statistics, virtual-camera crop geometry, and an FFmpeg-based `md_probe` tool. The video pipeline itself is not built yet. Numbers below are only those measured.

## Build (Ubuntu 24.04 / WSL2)
```bash
bash scripts/install_deps_ubuntu.sh      # once
cmake --preset debug-asan && cmake --build --preset debug-asan && ctest --preset debug-asan
cmake --preset debug-tsan && cmake --build --preset debug-tsan && setarch x86_64 -R ctest --preset debug-tsan  # ASLR off: see LESSONS.md
bash bench/run_bench.sh                  # release build + benchmarks, saved to bench/results/
```
Build outputs go to `~/build/match_director/<preset>`.

## Tools
- `md_probe <video>`: prints resolution, fps, codec, pixel format and duration of a clip.

## Measured results
Raw output with hardware details is in `bench/results/`. Phase 0 numbers (2026-11-02) come from an Intel i7-13700H (20 logical CPUs) under WSL2 Ubuntu 24.04, GCC 13.3, RelWithDebInfo, with 3 repetitions and the mean reported. They measure building blocks only. **No video pipeline numbers exist yet.**

| Benchmark | Result |
|---|---|
| SPSC push + pop, same thread | 1.15 ns per item |
| SPSC two-thread throughput (producer + consumer spinning) | 48.1 M items/s |
| SPSC one-way hand-off latency between threads | p50 234 ns, p99 294 ns, max 122–249 µs (OS scheduling spikes under WSL) |
| Virtual-camera crop compute + snap | 21.9 ns per frame |

Caveat: Ubuntu's `libbenchmark` package is itself a debug build (Google Benchmark prints a warning). That affects the harness, not our code.

## Docs
- [Architecture](docs/architecture.md)
- [Decisions](docs/decisions.md)
- [Dataset status](docs/dataset_status.md)
- [SPSC queue notes](docs/notes/spsc_queue.md)
- [Lessons / bugs found](LESSONS.md)
