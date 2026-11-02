# Lessons

Bugs found, how they were found, and what changed. Interview material, so be specific.

| Date | Bug / surprise | How found | Fix |
|---|---|---|---|
| 2026-11-02 | A ThreadSanitizer binary aborted at startup with `FATAL: ThreadSanitizer: unexpected memory mapping`, before any of our code ran | First TSAN run under WSL2 (kernel with high `vm.mmap_rnd_bits`). TSAN reserves fixed shadow-memory ranges, and heavy ASLR can place libraries inside them | Locally: run TSAN binaries with ASLR off: `setarch x86_64 -R ctest --preset debug-tsan`. In CI: `sysctl vm.mmap_rnd_bits=28` |
| 2026-11-02 | The TSAN *build* failed even though compiling and linking worked | `gtest_discover_tests` runs the freshly linked test binary to list its tests, and that run hit the ASLR abort above | `DISCOVERY_MODE PRE_TEST`: discovery now happens inside `ctest`, which runs under `setarch -R` |
