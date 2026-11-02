# Tracker and director: how it works, why, what can go wrong

Code: `src/track/*.cpp`, `src/director/director.cpp`. Tests: `tests/tracking_test.cpp`, `tests/director_test.cpp`.

## Tracker (SORT-style)
- **Kalman filter** per axis, with state (position, velocity), a constant-velocity model and white-noise acceleration. Two independent 2×2 filters instead of one 4×4, so all the maths is written out by hand in `Kalman1D`.
- **Association:** the cost is the distance between a track's predicted foot point and each detection, gated at 80 px. The Hungarian algorithm (O(n³), shortest augmenting path with potentials) finds the optimal one-to-one matching. It is verified against brute force on 200 random matrices.
- **Lifecycle:** a track is tentative until it has 3 hits; a tentative track dies on its first miss; a confirmed track survives 12 missed frames (short occlusions).

## Director
1. Weight each confirmed, currently seen track by `1 + speed / 120 px/s` (capped at 4). Moving players matter more.
2. Find the play: slide a window of 40% of the pitch width over the tracks sorted by x, and take the window with the most weight. This ignores a lone goalkeeper and stray false tracks.
3. Aim at the weighted centroid plus 0.35 s × the group's average velocity, so the camera leads the play.
4. Zoom: 2.6 × the group's spread plus a margin, clamped between the tightest and widest allowed crops.
5. **Dead zone:** the held target only changes when it moves more than 6% of the crop width. This stops jitter when play is calm.
6. **Critically damped springs** on x, y and zoom, using the exact closed-form step (so it's frame-rate independent, which is tested), with speed limits (900 px/s pan) and anti-windup at the frame edges.

## What can go wrong
- False tracks from shadows drag the centroid slightly. Weighting by density limits the damage.
- Fast switches of play hit the pan speed limit, so the camera lags for up to ~1 s. A real broadcast would cut to a wide shot instead.
- There's no lookahead. A delayed pipeline (decide using the next 1–2 s) would frame better and is a natural next step for a recorded or delayed broadcast.
- Quality is currently judged by proxies (pan speed, jerk, tracks in view) plus watching the output. It needs a labelled reference to measure properly.
