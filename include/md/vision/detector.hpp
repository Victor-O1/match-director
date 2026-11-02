#pragma once

#include "md/director/crop.hpp"
#include "md/vision/image.hpp"
#include "md/vision/pitch_mask.hpp"

#include <cstdint>
#include <vector>

namespace md {

struct Detection {
  RectF box;    // source pixels
  Vec2 foot;    // bottom-centre of the box, source pixels (ground contact point)
  int area{0};  // foreground pixels at analysis resolution
};

struct DetectorParams {
  int downscale{2};         // analysis resolution = source / downscale
  int diff_threshold{18};   // minimum |luma - background| that counts as foreground
  double noise_k{3.5};      // ...and it must also exceed noise_k * this pixel's typical deviation
  bool temporal_and{true};  // require foreground in 2 consecutive frames (kills flicker)
  int learn_shift_bg{6};    // background learning rate 1/2^n where background
  int learn_shift_fg{10};   // ...and (much slower) where foreground
  int min_area{12};         // blob size limits, analysis pixels
  int max_area{4000};
  int min_height{7};  // players stand: shadows and line shimmer are short and flat
  double max_width_over_height{1.3};
  int vertical_dilate{1};  // rows to dilate the mask up/down, so a player split into
                           // head/torso/legs fragments becomes one blob
  int warmup_frames{25};   // frames before detections are reported
};

// Moving-object detector for a FIXED camera, using background subtraction.
//
// Pipeline per frame (all buffers allocated once in the constructor):
//   1. box-downscale the luma plane,
//   2. per-pixel background model: running mean AND running mean-absolute-
//      deviation (fixed point, Q8). A pixel is foreground if it differs from
//      its mean by more than max(diff_threshold, noise_k * its deviation), so
//      pixels that always flicker (wet turf sparkle under floodlights,
//      compression noise on goal nets) need a much bigger change to trigger,
//   3. temporal AND with the previous frame's mask: real movers persist,
//      single-frame sparkle does not,
//   4. update the model: fast where it looks like background, very slow
//      where it looks like foreground, so standing players don't vanish instantly,
//   5. small vertical dilation (joins head/torso/legs fragments of one player),
//   6. 8-connected components (two-pass, union-find) on the foreground mask,
//   7. filter blobs by area and SHAPE (players are upright: min height, not
//      wider than tall). That removes most floodlight shadows, which are short
//      and flat, and shimmer along painted lines. See docs/notes/detector.md.
//
// Why not a neural detector? No NVIDIA GPU on the dev machine, the camera never
// moves, and this runs in a few ms per frame on one core. The Detector output type
// is the seam where an ONNX model can be swapped in later.
class BackgroundDetector {
 public:
  BackgroundDetector(int source_width, int source_height, PitchMask mask,
                     DetectorParams params = {});

  // `luma` is the full-resolution Y plane. Returns detections for this frame.
  const std::vector<Detection>& detect(GrayView luma);

  [[nodiscard]] const GrayImage& foreground() const { return fg_; }
  [[nodiscard]] const GrayImage& small_frame() const { return small_; }
  [[nodiscard]] int analysis_width() const { return small_.width; }
  [[nodiscard]] int analysis_height() const { return small_.height; }

 private:
  void label_components();

  DetectorParams p_;
  int src_w_;
  int src_h_;
  PitchMask mask_;
  GrayImage small_;
  GrayImage fg_;
  std::vector<std::uint16_t> bg_;       // Q8 fixed-point background mean
  std::vector<std::uint16_t> dev_;      // Q8 fixed-point mean absolute deviation
  std::vector<std::uint8_t> raw_prev_;  // previous frame's raw (pre-AND) mask
  GrayImage fg_and_;                    // mask after the temporal AND, before dilation
  std::vector<std::int32_t> labels_;
  std::vector<std::int32_t> parent_;  // union-find forest, indexed by provisional label
  struct BlobAcc {
    int min_x, min_y, max_x, max_y, area;
  };
  std::vector<BlobAcc> acc_;
  std::vector<Detection> out_;
  std::int64_t frames_{0};
};

}  // namespace md
