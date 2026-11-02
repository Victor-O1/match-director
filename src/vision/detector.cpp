#include "md/vision/detector.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

namespace md {

namespace {

std::int32_t find_root(std::vector<std::int32_t>& parent, std::int32_t i) {
  // Path halving: every node on the way points to its grandparent.
  while (parent[static_cast<std::size_t>(i)] != i) {
    auto& p = parent[static_cast<std::size_t>(i)];
    p = parent[static_cast<std::size_t>(p)];
    i = p;
  }
  return i;
}

void unite(std::vector<std::int32_t>& parent, std::int32_t a, std::int32_t b) {
  a = find_root(parent, a);
  b = find_root(parent, b);
  if (a != b) {
    // Smaller label becomes the root, so roots are stable and deterministic.
    if (a < b) {
      parent[static_cast<std::size_t>(b)] = a;
    } else {
      parent[static_cast<std::size_t>(a)] = b;
    }
  }
}

}  // namespace

BackgroundDetector::BackgroundDetector(int source_width, int source_height, PitchMask mask,
                                       DetectorParams params)
    : p_(params),
      src_w_(source_width),
      src_h_(source_height),
      mask_(std::move(mask)),
      small_(source_width / params.downscale, source_height / params.downscale),
      fg_(small_.width, small_.height),
      fg_and_(small_.width, small_.height) {
  const auto n = small_.pixels.size();
  bg_.assign(n, 0);
  dev_.assign(n, 0);
  raw_prev_.assign(n, 0);
  labels_.assign(n, 0);
  // Worst case (checkerboard) is one provisional label per two pixels.
  parent_.reserve(n / 2 + 2);
  acc_.reserve(n / 2 + 2);
  out_.reserve(256);
  if (mask_.image().width != small_.width || mask_.image().height != small_.height) {
    mask_ = PitchMask({}, small_.width, small_.height);
  }
}

const std::vector<Detection>& BackgroundDetector::detect(GrayView luma) {
  downscale_box(luma, p_.downscale, small_);
  out_.clear();

  const std::size_t n = small_.pixels.size();
  const std::uint8_t* cur = small_.pixels.data();
  const std::uint8_t* inside = mask_.image().pixels.data();
  std::uint8_t* fg = fg_and_.pixels.data();

  if (frames_ == 0) {
    for (std::size_t i = 0; i < n; ++i) {
      bg_[i] = static_cast<std::uint16_t>(cur[i] << 8);
      dev_[i] = static_cast<std::uint16_t>(4 << 8);  // assume mild noise until learned
    }
  }
  ++frames_;

  const int thr_min = p_.diff_threshold << 8;                   // Q8
  const auto k_q4 = static_cast<int>(p_.noise_k * 16.0 + 0.5);  // noise_k in Q4
  const int s_bg = p_.learn_shift_bg;
  const int s_fg = p_.learn_shift_fg;
  const bool use_and = p_.temporal_and;
  for (std::size_t i = 0; i < n; ++i) {
    const int c = cur[i] << 8;  // Q8
    const int b = bg_[i];
    const int d = dev_[i];
    const int diff = std::abs(c - b);
    // threshold = max(min, k * dev), all in Q8 (k is Q4, so shift back by 4).
    const int thr = std::max(thr_min, (k_q4 * d) >> 4);
    const bool raw = inside[i] != 0 && diff > thr;
    fg[i] = (raw && (!use_and || raw_prev_[i] != 0)) ? 255 : 0;
    raw_prev_[i] = raw ? 1 : 0;
    // Running averages: x += (target - x) / 2^shift. Arithmetic right shift
    // of a negative delta rounds towards -inf (1/256 of a grey level: harmless).
    const int shift = raw ? s_fg : s_bg;
    bg_[i] = static_cast<std::uint16_t>(b + ((c - b) >> shift));
    // Deviation only learns from background-looking pixels, otherwise a player
    // walking past would teach the model that this pixel is "noisy".
    if (!raw) {
      dev_[i] = static_cast<std::uint16_t>(d + ((diff - d) >> s_bg));
    }
  }

  if (frames_ <= p_.warmup_frames) {
    return out_;
  }

  // Vertical dilation: fg_[y] = max(fg_and_[y - r .. y + r]).
  const int w = small_.width;
  const int h = small_.height;
  const int r = std::max(0, p_.vertical_dilate);
  for (int y = 0; y < h; ++y) {
    std::uint8_t* out = fg_.row(y);
    std::copy_n(fg_and_.row(y), w, out);
    for (int dy = -r; dy <= r; ++dy) {
      const int yy = y + dy;
      if (dy == 0 || yy < 0 || yy >= h) continue;
      const std::uint8_t* in = fg_and_.row(yy);
      for (int x = 0; x < w; ++x) {
        out[x] = std::max(out[x], in[x]);
      }
    }
  }

  label_components();

  const double sx = static_cast<double>(src_w_) / small_.width;
  const double sy = static_cast<double>(src_h_) / small_.height;
  for (const BlobAcc& a : acc_) {
    if (a.area < p_.min_area || a.area > p_.max_area) {
      continue;
    }
    const int bw = a.max_x - a.min_x + 1;
    const int bh = a.max_y - a.min_y + 1;
    if (bh < p_.min_height || static_cast<double>(bw) > p_.max_width_over_height * bh) {
      continue;  // short or flat: shadows, line shimmer, compression noise
    }
    Detection d;
    d.box = {a.min_x * sx, a.min_y * sy, bw * sx, bh * sy};
    d.foot = {d.box.x + d.box.width / 2.0, d.box.y + d.box.height};
    d.area = a.area;
    out_.push_back(d);
  }
  return out_;
}

void BackgroundDetector::label_components() {
  const int w = fg_.width;
  const int h = fg_.height;
  const std::uint8_t* fg = fg_.pixels.data();
  parent_.clear();
  parent_.push_back(0);  // label 0 = background

  // Pass 1: provisional labels from the already-visited neighbours
  // (W, NW, N, NE for 8-connectivity), recording equivalences.
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const auto i =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
      if (fg[i] == 0) {
        labels_[i] = 0;
        continue;
      }
      std::int32_t best = 0;
      auto consider = [&](int nx, int ny) {
        if (nx < 0 || ny < 0 || nx >= w) {
          return;
        }
        const std::int32_t l = labels_[static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) +
                                       static_cast<std::size_t>(nx)];
        if (l == 0) {
          return;
        }
        if (best == 0) {
          best = l;
        } else if (l != best) {
          unite(parent_, best, l);
        }
      };
      consider(x - 1, y);
      consider(x - 1, y - 1);
      consider(x, y - 1);
      consider(x + 1, y - 1);
      if (best == 0) {
        best = static_cast<std::int32_t>(parent_.size());
        parent_.push_back(best);
      }
      labels_[i] = best;
    }
  }

  // Pass 2: accumulate stats per root label. acc_ is indexed by root label;
  // unused entries keep area 0 and are compacted afterwards.
  const std::size_t n_labels = parent_.size();
  acc_.assign(n_labels,
              BlobAcc{std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), -1, -1, 0});
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const auto i =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
      const std::int32_t l = labels_[i];
      if (l == 0) {
        continue;
      }
      BlobAcc& a = acc_[static_cast<std::size_t>(find_root(parent_, l))];
      a.min_x = std::min(a.min_x, x);
      a.min_y = std::min(a.min_y, y);
      a.max_x = std::max(a.max_x, x);
      a.max_y = std::max(a.max_y, y);
      ++a.area;
    }
  }
  acc_.erase(std::remove_if(acc_.begin(), acc_.end(), [](const BlobAcc& a) { return a.area == 0; }),
             acc_.end());
}

}  // namespace md
