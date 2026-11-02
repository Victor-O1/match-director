#pragma once

#include "md/media/av_ptr.hpp"

#include <cstdint>
#include <vector>

namespace md {

// Rolling buffer of the last N compressed video packets: the replay store.
//
// Keeping COMPRESSED packets instead of decoded frames is what makes a long
// buffer cheap: 30 s of this 4K stream is ~75 MB as H.264, but would be
// ~8 GB as decoded YUV. A replay re-decodes from the nearest keyframe.
//
// av_packet_ref() only bumps a reference count, so storing a packet does not
// copy its payload. Slots are preallocated; the ring never allocates packet
// shells after construction.
//
// Single-threaded: owned by the decode thread.
class PacketRing {
 public:
  explicit PacketRing(std::size_t capacity);

  void push(const AVPacket* pkt);

  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] std::size_t capacity() const { return slots_.size(); }

  // i = 0 is the oldest packet held.
  [[nodiscard]] const AVPacket* at(std::size_t i) const;

  // Position (0 = oldest) of the last keyframe with pts <= `pts`, or -1 if the
  // buffer doesn't reach back that far.
  [[nodiscard]] std::int64_t keyframe_at_or_before(std::int64_t pts) const;

  [[nodiscard]] std::int64_t oldest_pts() const;
  [[nodiscard]] std::int64_t newest_pts() const;

 private:
  std::vector<AvPacketPtr> slots_;
  std::size_t head_{0};  // index of the oldest packet
  std::size_t count_{0};
};

}  // namespace md
