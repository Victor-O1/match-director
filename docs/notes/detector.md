# Detector: how it works, why, what can go wrong

Code: `src/vision/detector.cpp`. Tests: `tests/detector_test.cpp`.

## How it works
1. Downscale the 4K luma plane by 2 (box filter). This gives 1920×953, where players are ~5–30 px tall.
2. **Per-pixel background model**, in fixed point (Q8, `uint16_t`). Each pixel keeps a running mean and a running mean-absolute-deviation. It is foreground if `|pixel − mean| > max(18, 3.5 × deviation)`.
3. **Temporal AND:** a pixel must be foreground in two consecutive frames.
4. Model update: the mean learns fast (1/64) on background pixels and very slowly (1/1024) on foreground pixels. The deviation learns only from background pixels.
5. Vertical dilation by one row, so a player split into head, torso and legs joins into one blob.
6. Connected components, two-pass with union-find (path halving), 8-connectivity.
7. Shape filter: area 10–3000, height ≥ 7, width ≤ 1.3 × height.
8. Only pixels inside the pitch polygon (`config/*.json`) are considered.

## Why
- The camera never moves, so "what changed" is a strong cue, and it costs ~11 ms/frame on one core. There's no NVIDIA GPU for a neural detector.
- Step 2 fixes wet-turf sparkle under floodlights: noisy pixels learn a higher threshold.
- Step 3 removes single-frame flicker.
- Step 7 removes most floodlight **shadows** (four lights give every player several long, flat moving shadows) and shimmer along painted lines.
- Learning slowly on foreground pixels keeps players who stand still from vanishing within a second.

## What can go wrong (and did)
- **Shadows still leak:** about 42 tracks vs ~23 people (see README).
- Players who stand still for a long time slowly blend into the background.
- Players merge into one blob when they overlap. That's fine for framing the play, but not for counting players.
- A global lighting change (floodlights switching, camera auto-exposure) makes the whole pitch foreground for a moment.
- The polygon is hand-traced for one camera position; a new venue needs a new one.
- **Next step:** an ONNX person detector on tiles, with background subtraction as a cheap "where to look" prior.
