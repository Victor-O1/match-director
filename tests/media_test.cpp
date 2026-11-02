#include "md/gfx/overlay.hpp"
#include "md/replay/packet_ring.hpp"

#include <gtest/gtest.h>

namespace {

md::AvPacketPtr fake_packet(std::int64_t pts, bool key) {
  md::AvPacketPtr p = md::make_packet();
  p->pts = pts;
  p->dts = pts;
  p->flags = key ? AV_PKT_FLAG_KEY : 0;
  return p;
}

TEST(PacketRing, KeepsNewestAndFindsKeyframe) {
  md::PacketRing ring(10);
  for (int i = 0; i < 25; ++i) {
    auto p = fake_packet(i, i % 5 == 0);  // keyframe every 5
    ring.push(p.get());
  }
  EXPECT_EQ(ring.size(), 10U);
  EXPECT_EQ(ring.oldest_pts(), 15);
  EXPECT_EQ(ring.newest_pts(), 24);
  // Replay starting at pts 18 must decode from keyframe 15 (position 0).
  EXPECT_EQ(ring.keyframe_at_or_before(18), 0);
  EXPECT_EQ(ring.at(static_cast<std::size_t>(ring.keyframe_at_or_before(22)))->pts, 20);
  EXPECT_EQ(ring.keyframe_at_or_before(10), -1) << "older than the buffer";
}

TEST(Overlay, ClockFormatting) {
  EXPECT_EQ(md::format_clock(0), "00:00");
  EXPECT_EQ(md::format_clock(-5), "00:00");
  EXPECT_EQ(md::format_clock(754.9), "12:34");
  EXPECT_EQ(md::format_clock(1e9), "99:59");
}

TEST(Overlay, RectIsTwoTriangles) {
  md::OverlayBatch b;
  b.rect(10, 20, 30, 40, md::rgba(1, 2, 3));
  ASSERT_EQ(b.vertices().size(), 6U);
  EXPECT_FLOAT_EQ(b.vertices()[4].x, 40.f);
  EXPECT_FLOAT_EQ(b.vertices()[4].y, 60.f);
  EXPECT_EQ(b.vertices()[0].rgba, 0xFF030201u);
}

}  // namespace
