# Instant replay: how it works, why, what can go wrong

Code: `src/replay/packet_ring.cpp`, `Pipeline::run_replay`, and the encoder thread's clip export.

## How it works
- The decode thread keeps the last 30 s of **compressed** video packets in `PacketRing`. `av_packet_ref` only bumps a reference count, so the ring never copies payloads, and its slots are preallocated.
- **Triggers:**
  - A goal at live time *t* schedules a replay of [t−6 s, t+1 s] that rolls at t+3 s (the celebration).
  - `{"type":"replay","seconds":8}` replays the last 8 s immediately.
- **Replay:** find the last keyframe at or before the window start; decode with a **second** decoder, so the live decoder's reference frames are untouched; drop frames before the window (they are decoded only as references); emit the rest with `repeat = 2` (half speed); fade in and out over 6 frames.
- The analysis thread doesn't re-detect replay frames. It looks up the camera path it recorded live (by source frame index) and tightens it to 72% width: same framing, closer.
- The encoder writes replay frames into the program and also into `replays/replay_NNN.mp4`.

## Why compressed packets
30 s of this 4K stream is about 75 MB as H.264, but about 8 GB as decoded YUV. Re-decoding 7–8 s of 4K on demand is fast (the decoder runs at ~70+ fps).

## Measured
3 replays in the 5-minute run, each 201 source frames, all 3 clips written. "Mark → clip ready" was 3.3–4.8 s in file mode. That figure includes the 3 s post-goal delay, which in file mode passes faster than real time, so it's **not** a live latency number. Live mode would add playout time.

## What can go wrong
- A replay longer than the ring (or one requested right after start) is clipped to what's buffered.
- Keyframes here are every 2 s, so up to 2 s of extra decoding before the first replay frame.
- Live content isn't shown during a replay (the timeline shifts). A real system would show a delayed feed or picture-in-picture.
