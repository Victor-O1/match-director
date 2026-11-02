# Architecture

## Threads

| Thread | Owns | Input | Output |
|---|---|---|---|
| decode | `MediaSource` (demux + decoder), `PacketRing`, replay decoder, the **output timeline** | file / URL, operator inbox, events file | `FrameMsg` (live and replay frames), `Command`s, audio packets |
| analysis | `BackgroundDetector`, `Tracker`, `Director`, camera history | `FrameMsg` | `FrameMsg` + crop |
| compositor | EGL context, `Compositor`, `ScoreboardState` | `FrameMsg`, `Command`s | `OutMsg` (NV12 picture + metadata) |
| encoder | `ProgramOutput` (x264 + muxer), replay clip writer, stats | `OutMsg`, audio packets | file / stream |
| http (optional) | `HttpServer` | sockets | operator inbox (mutex: cold path) |

## Queues (all bounded, single producer / single consumer)

| Channel | Capacity | Carries |
|---|---|---|
| decoded_ | 16 | decode → analysis |
| analysed_ | 16 | analysis → compositor |
| rendered_ | 8 | compositor → encoder |
| commands_ | 64 | decode → compositor |
| frame_return_ | 32 | compositor → decode (free decoded-frame shells) |
| out_return_ | 16 | encoder → compositor (free NV12 buffers) |
| audio_ / audio_return_ | 1024 | decode ↔ encoder |

The pools (16 decoded-frame shells, 12 NV12 outputs, 1024 audio packets) are allocated in the `Pipeline` constructor and circulate through the return channels. **Pool size is the back-pressure.** If the encoder is slow, NV12 buffers run out, the compositor blocks, then analysis blocks, then decode. Memory stays flat.

## Frame-accurate commands
The decode thread is the only writer of the timeline. It stamps each command with an increasing `seq` and pushes it **before** the frame it applies to. Every frame carries the latest `cmd_seq`. The compositor applies commands until it reaches the frame's `cmd_seq`, so a goal or caption appears on exactly the right frame, even though the threads run at different speeds.

## Timeline and replays
`out_index` counts output frames. A replay is decoded from the packet ring by a second decoder, starting at the keyframe before the replay window. Frames before the window are decoded (needed as references) but dropped. Each replay frame gets `repeat = 2` (half speed), so the encoder writes it twice. Replay time is added to the audio timestamps, so live audio stays in sync after a replay.

## Timing and stats
Every frame carries `StageTimes` from one monotonic clock. The encoder records ingest → muxer latency for live frames. Each thread records its own work time per frame into a preallocated `LatencyRecorder`; these are read only after `join()`, so they need no locks.

## Shutdown
- **Normal end:** decode closes `decoded_`, `audio_` and `commands_`. Each later stage drains its input, then closes its output. All threads are joined, then pools are freed.
- **Error:** the failing stage records the first error and closes **every** channel, so all blocked threads wake up. `run()` rethrows after the join, and the program exits 1.
