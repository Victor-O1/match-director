# Decisions log

Each entry: what was decided, why, and what would make us revisit it.

## 2026-11-02 — Toolchain: WSL2 Ubuntu 24.04, GCC 13, CMake + Ninja, apt packages
- **Why:** The dev machine (i7-13700H, 16 GB, Intel Iris Xe, **no NVIDIA GPU**) runs Windows. The only Windows compiler was MinGW GCC 6.3, which is too old for C++20. WSL Ubuntu gives GCC 13, all three sanitizers (ASAN, UBSAN, TSAN), valgrind and perf, plus FFmpeg/GLFW/FreeType dev packages from apt. Most broadcast/embedded C++ work is Linux.
- **Package manager:** apt for system libraries, not vcpkg/Conan. CI uses the same `scripts/install_deps_ubuntu.sh` on `ubuntu-24.04`, so the Ubuntu release pins the versions. **Revisit** if we need a library version apt doesn't have (likely ONNX Runtime, which ships as a prebuilt tarball we'll pin by version + SHA256), or when we add a native Windows build.
- **Build dirs** live in WSL's `$HOME/build/match_director/<preset>`, not in `/mnt/c`, because cross-filesystem builds are slow.

## 2026-11-02 — No NVIDIA GPU, so the CPU path is primary
- Decode: FFmpeg software decode (Intel QSV/VAAPI under WSL is unreliable, so we don't depend on it).
- Inference: ONNX Runtime CPU EP with a small detector, run on a downscaled frame + tiles.
- Rendering: OpenGL through Mesa in WSL (WSLg's d3d12 driver on Iris Xe), or offscreen EGL.
- Encode: x264 through libavcodec.
- NVENC/NVDEC/TensorRT stay stretch goals, and the code keeps stage interfaces so a GPU backend can drop in. The README must say which numbers are CPU-only.

## 2026-11-02 — C++20, warnings as errors
- `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow ...` with `-Werror` (toggle `MD_WARNINGS_AS_ERRORS`). FFmpeg headers come in as system includes, so their warnings don't count.

## 2026-11-02 — Inter-stage queues: our own bounded SPSC ring
- Every pipeline edge has exactly one producer thread and one consumer thread, so SPSC is enough. It is wait-free, allocation-free, and small enough to explain line by line in an interview. See `docs/notes/spsc_queue.md`.
- Alternatives considered: `moodycamel::ReaderWriterQueue` (well tested, but a dependency we couldn't explain as deeply) and mutex + condition_variable (simpler, but it takes a lock on every frame and wakes the scheduler). **Revisit** if a stage needs fan-out (e.g. decoded frames going to both the replay buffer and inference). Then frames become ref-counted handles pushed into two SPSC queues.

## 2026-11-02 — Input footage: user's own panoramic match recordings
- The user has their own panoramic football recordings in several Drive folders (two halves per match). They are the primary dev/demo data. SoccerTrack v2 stays a candidate for ground-truth evaluation. Status in `docs/dataset_status.md`.

## 2026-11-02 — Detector: background subtraction, not a neural network (for now)
- **Options:** (a) background subtraction (fixed camera, CPU, ~11 ms/frame); (b) YOLO-family ONNX on CPU (slow on 4K without a GPU, needs tiling; Ultralytics weights are AGPL); (c) wait for an NVIDIA GPU.
- **Chose (a)** to finish the pipeline end to end. The `Detection` struct is the seam for (b). Known cost: shadows cause false tracks (see LESSONS.md).

## 2026-11-02 — Graphics: OpenGL ES 3.0 via EGL surfaceless, Mesa D3D12 on WSL
- The real Iris Xe GPU is reachable from WSL2 through Mesa's D3D12 Gallium driver (ES 3.0 only). The same code runs on llvmpipe in CI. Vulkan was out of scope.

## 2026-11-02 — Replay store: compressed packets + second decoder
- ~75 MB for 30 s vs ~8 GB of decoded 4K frames. Re-decoding is fast enough. A second decoder keeps the live decoder's state untouched.

## 2026-11-02 — Control API: own minimal HTTP server
- ~200 lines on POSIX sockets, loopback by default, one connection at a time. It avoids pulling in a web framework for one operator. **Revisit** for multi-user or internet-facing use (TLS, auth): use a proper library there.

## 2026-11-02 — `--realtime` defaults to 4 decode threads
- Measured latency/throughput trade-off (LESSONS.md).
