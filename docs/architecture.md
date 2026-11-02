# Architecture

Status: Phase 0. Only the pieces marked **[built]** exist. Everything else is the plan.

```
[Input: file / RTSP / SRT]
   │ FFmpeg demux + decode (CPU)
   ▼
[Frame pool + timestamps]  ──►  [Replay ring: last N min, mark → clip → slow-mo]
   │ SPSC [built]
   ▼
[Detector: players + ball (ONNX Runtime, CPU, tiled for the small ball)]
   ▼
[Tracker: Kalman + Hungarian (ByteTrack-style)]
   ▼
[Director: action point → smoothed virtual camera → crop [built: crop geometry]]
   ▼
[Compositor: OpenGL crop + FreeType text + scoreboard from JSON]
   ▼
[Encoder: x264 → file / RTMP / SRT]
        ▲
[Control plane: REST/WebSocket (score, replay marks) + web panel]
```

## Threads and queues
One thread per stage. Each edge is a bounded `SpscQueue` that carries frame *handles*, never pixel data. Queues are bounded so a slow stage applies back-pressure (or drops frames by policy) instead of growing memory.

## Timing
Each frame carries `StageTimes` (`include/md/core/clock.hpp`), stamped from one monotonic clock. End-to-end latency = `output_ns - ingest_ns`. Per-stage latency is the difference between neighbouring stamps. `LatencyRecorder` collects these without allocating.

## Rules
- No unbounded queues. No heap allocation per frame on hot paths (pool everything).
- Every stage stamps its frames. Every performance claim comes from `bench/`.
- Clean shutdown: stop the input, drain the queues in pipeline order, join the threads, then destroy the queues.
- No leaks over a 3-hour soak (to be run in Week 4).
