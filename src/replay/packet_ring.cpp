#include "md/replay/packet_ring.hpp"

#include <stdexcept>

namespace md {

PacketRing::PacketRing(std::size_t capacity) {
  if (capacity == 0) {
    throw std::invalid_argument("PacketRing capacity must be > 0");
  }
  slots_.reserve(capacity);
  for (std::size_t i = 0; i < capacity; ++i) {
    slots_.push_back(make_packet());
  }
}

void PacketRing::push(const AVPacket* pkt) {
  const std::size_t cap = slots_.size();
  std::size_t idx = 0;
  if (count_ < cap) {
    idx = (head_ + count_) % cap;
    ++count_;
  } else {
    idx = head_;  // overwrite the oldest
    head_ = (head_ + 1) % cap;
  }
  AVPacket* slot = slots_[idx].get();
  av_packet_unref(slot);
  av_check(av_packet_ref(slot, pkt), "packet ref");
}

const AVPacket* PacketRing::at(std::size_t i) const {
  return slots_[(head_ + i) % slots_.size()].get();
}

std::int64_t PacketRing::keyframe_at_or_before(std::int64_t pts) const {
  for (std::size_t i = count_; i-- > 0;) {
    const AVPacket* p = at(i);
    if ((p->flags & AV_PKT_FLAG_KEY) != 0 && p->pts <= pts) {
      return static_cast<std::int64_t>(i);
    }
  }
  return -1;
}

std::int64_t PacketRing::oldest_pts() const { return count_ == 0 ? 0 : at(0)->pts; }
std::int64_t PacketRing::newest_pts() const { return count_ == 0 ? 0 : at(count_ - 1)->pts; }

}  // namespace md
