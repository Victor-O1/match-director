# Match Director: working rules for Claude Code

- Project: automated sports broadcast in C++20 (virtual camera director, GPU graphics, instant replay, encoder). Also called AutoCast. See README and docs/architecture.md.
- Build/test in **WSL Ubuntu** only: `wsl -d Ubuntu -- bash -lc "cd <repo> && cmake --preset debug-asan && cmake --build --preset debug-asan && ctest --preset debug-asan"`. Presets: debug-asan, debug-tsan, release. Build dirs are in WSL `~/build/match_director/`. Run TSAN tests with `setarch x86_64 -R ctest --preset debug-tsan`, because TSAN aborts under WSL's ASLR otherwise.
- Machine has **no NVIDIA GPU** (Intel Iris Xe). The CPU path is primary.
- Work in small vertical slices: each one builds, has tests and a benchmark, is sanitizer-clean, and ends in a commit. Report measured numbers after each slice.
- Never invent numbers, dataset properties or licences. Write "unknown — to be measured".
- Every component gets a "how it works / why / what could go wrong" note in docs/notes/. Log real bugs in LESSONS.md. Record trade-off decisions in docs/decisions.md.
- For real trade-offs, offer the user 2–3 options with a recommendation instead of picking silently.
- Hot paths: no per-frame heap allocation, no unbounded queues, no locks in SPSC edges. Modern C++: RAII, no raw new/delete.
- Never commit video/model files, secrets, or personal details.
