#include "md/core/clock.hpp"
#include "md/core/latency_stats.hpp"
#include "md/core/spsc_queue.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace {

using Queue = md::SpscQueue<std::uint64_t, 1024>;

// Cost of one push + one pop on the same thread (no contention, cache-hot).
void BM_SpscPushPopSameThread(benchmark::State& state) {
  auto q = std::make_unique<Queue>();
  std::uint64_t v = 0;
  std::uint64_t out = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(q->try_push(std::uint64_t{v++}));
    benchmark::DoNotOptimize(q->try_pop(out));
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SpscPushPopSameThread);

// Sustained throughput with a real producer thread and consumer thread.
// Both spin (no yield) so this measures the queue, not the OS scheduler.
void BM_SpscTwoThreadThroughput(benchmark::State& state) {
  constexpr std::uint64_t kItems = 2'000'000;
  for (auto _ : state) {
    auto q = std::make_unique<Queue>();
    std::thread producer([&q] {
      for (std::uint64_t i = 0; i < kItems; ++i) {
        std::uint64_t v = i;
        while (!q->try_push(std::move(v))) {
        }
      }
    });
    std::uint64_t received = 0;
    std::uint64_t out = 0;
    while (received < kItems) {
      if (q->try_pop(out)) {
        ++received;
      }
    }
    producer.join();
    benchmark::DoNotOptimize(out);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kItems));
}
BENCHMARK(BM_SpscTwoThreadThroughput)->Unit(benchmark::kMillisecond)->UseRealTime();

// One-way hand-off latency: producer stamps now_ns() into the item, consumer
// computes now_ns() - stamp. Reports percentiles as counters (in ns).
void BM_SpscHandoffLatency(benchmark::State& state) {
  constexpr int kItems = 200'000;
  md::LatencyRecorder rec(static_cast<std::size_t>(kItems));
  for (auto _ : state) {
    rec.reset();
    auto q = std::make_unique<md::SpscQueue<std::int64_t, 1024>>();
    std::atomic<bool> consumed{true};
    std::thread consumer([&] {
      std::int64_t stamp = 0;
      for (int n = 0; n < kItems;) {
        if (q->try_pop(stamp)) {
          rec.record(md::now_ns() - stamp);
          consumed.store(true, std::memory_order_release);
          ++n;
        }
      }
    });
    // Send one item at a time so we measure hand-off, not queueing delay.
    for (int i = 0; i < kItems; ++i) {
      while (!consumed.load(std::memory_order_acquire)) {
      }
      consumed.store(false, std::memory_order_relaxed);
      std::int64_t stamp = md::now_ns();
      while (!q->try_push(std::move(stamp))) {
      }
    }
    consumer.join();
  }
  const auto s = rec.summarize();
  state.counters["p50_ns"] = static_cast<double>(s.p50_ns);
  state.counters["p99_ns"] = static_cast<double>(s.p99_ns);
  state.counters["max_ns"] = static_cast<double>(s.max_ns);
}
BENCHMARK(BM_SpscHandoffLatency)->Unit(benchmark::kMillisecond)->UseRealTime()->Iterations(3);

}  // namespace
