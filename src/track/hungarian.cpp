#include "md/track/hungarian.hpp"

#include <algorithm>
#include <limits>

namespace md {

const std::vector<int>& HungarianSolver::solve(const std::vector<double>& cost, std::size_t rows,
                                               std::size_t cols) {
  result_.assign(rows, -1);
  if (rows == 0 || cols == 0) {
    return result_;
  }

  // Square n x n problem; padding cells cost 0 so they never distort the
  // choice among real cells.
  const std::size_t n = std::max(rows, cols);
  padded_.assign(n * n, 0.0);
  for (std::size_t r = 0; r < rows; ++r) {
    std::copy_n(cost.begin() + static_cast<std::ptrdiff_t>(r * cols), cols,
                padded_.begin() + static_cast<std::ptrdiff_t>(r * n));
  }

  // 1-indexed arrays; index 0 is a virtual column used as the path root.
  constexpr double kInf = std::numeric_limits<double>::infinity();
  u_.assign(n + 1, 0.0);  // row potentials
  v_.assign(n + 1, 0.0);  // column potentials
  p_.assign(n + 1, 0);    // p_[col] = row matched to col (0 = none)
  way_.assign(n + 1, 0);  // previous column on the augmenting path
  minv_.resize(n + 1);
  used_.resize(n + 1);

  for (std::size_t i = 1; i <= n; ++i) {
    // Add row i: grow a shortest-path tree of reduced costs until we reach a
    // free column, then flip the path.
    p_[0] = i;
    std::size_t j0 = 0;
    std::fill(minv_.begin(), minv_.end(), kInf);
    std::fill(used_.begin(), used_.end(), char{0});
    do {
      used_[j0] = 1;
      const std::size_t i0 = p_[j0];
      double delta = kInf;
      std::size_t j1 = 0;
      for (std::size_t j = 1; j <= n; ++j) {
        if (used_[j] != 0) {
          continue;
        }
        const double cur = padded_[(i0 - 1) * n + (j - 1)] - u_[i0] - v_[j];
        if (cur < minv_[j]) {
          minv_[j] = cur;
          way_[j] = j0;
        }
        if (minv_[j] < delta) {
          delta = minv_[j];
          j1 = j;
        }
      }
      for (std::size_t j = 0; j <= n; ++j) {
        if (used_[j] != 0) {
          u_[p_[j]] += delta;
          v_[j] -= delta;
        } else {
          minv_[j] -= delta;
        }
      }
      j0 = j1;
    } while (p_[j0] != 0);
    do {
      const std::size_t j1 = way_[j0];
      p_[j0] = p_[j1];
      j0 = j1;
    } while (j0 != 0);
  }

  for (std::size_t j = 1; j <= n; ++j) {
    const std::size_t r = p_[j] - 1;
    if (r < rows && j - 1 < cols) {
      result_[r] = static_cast<int>(j - 1);
    }
  }
  return result_;
}

}  // namespace md
