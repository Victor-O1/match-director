# SPSC queue: how it works, why, what can go wrong

Code: `include/md/core/spsc_queue.hpp`. Tests: `tests/spsc_queue_test.cpp`. Bench: `bench/spsc_bench.cpp`.

## How it works
- A fixed array of `Capacity` slots (a power of two, so `i % Capacity` is just `i & mask`).
- Two counters only ever increase: `tail_` (next slot to write, owned by the producer) and `head_` (next slot to read, owned by the consumer). `tail_ - head_` is the number of items in the queue.
- **Push:** if `tail - head == Capacity` the queue is full. Otherwise write the slot, then `tail_.store(tail+1, release)`.
- **Pop:** if `head == tail` the queue is empty. Otherwise move out of the slot, then `head_.store(head+1, release)`.

## Why those memory orders
- The producer writes the slot (a plain write) *before* the release store to `tail_`. The consumer's acquire load of `tail_` then guarantees it sees that slot write. Without release/acquire, the CPU or compiler could reorder, and the consumer could read a half-written frame.
- The same pairing in the other direction (consumer release-stores `head_`, producer acquire-loads it) guarantees the consumer has finished moving out of a slot before the producer overwrites it.
- Each side loads its *own* counter with `relaxed`, because only that thread ever writes it.

## Why cached indices
Each side keeps a plain copy of the other side's counter and only re-reads the shared atomic when the copy says full or empty. Without this, every push reads the consumer's cache line and every pop reads the producer's, so the two lines bounce between cores on every operation.

## Why `alignas(64)`
`head_` and `tail_` are written by different cores. If they shared a 64-byte cache line, each write would invalidate the other core's copy (false sharing), even though they're different variables.

## What could go wrong
- **Two producers or two consumers:** a data race. The type can't stop this; it is documented. TSAN catches it in tests.
- **Non-power-of-two capacity:** rejected at compile time with `static_assert`.
- **Large `T` stored by value:** a 4K frame per slot would be huge, and moving it is a big copy. Queues carry *handles* (pointers/indices into a preallocated frame pool), never pixel buffers.
- **Busy-waiting:** spinning on a full or empty queue burns a core. Pipeline stages will block on a condition or eventfd when idle. The queue itself stays non-blocking.
- **Destruction while the other thread is running:** undefined behaviour. Shutdown order is documented in `docs/architecture.md`.
