#pragma once

#include <cstddef>
#include <vector>

namespace md {

// Optimal assignment (minimum total cost) for a rows x cols cost matrix,
// using the O(n^3) Hungarian algorithm with potentials (Jonker-Volgenant style
// shortest augmenting paths). Rectangular matrices are padded internally.
//
// Returns, for each row, the assigned column or -1 if the row got a padding
// column (more rows than columns). `cost` is row-major, rows * cols.
//
// The solver keeps its scratch buffers between calls, so steady-state calls
// with similar sizes don't allocate.
class HungarianSolver {
 public:
  const std::vector<int>& solve(const std::vector<double>& cost, std::size_t rows,
                                std::size_t cols);

 private:
  std::vector<double> u_, v_, minv_, padded_;
  std::vector<std::size_t> p_, way_;
  std::vector<char> used_;
  std::vector<int> result_;
};

}  // namespace md
