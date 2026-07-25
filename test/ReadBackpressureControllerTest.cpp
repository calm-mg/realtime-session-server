#include <gtest/gtest.h>

#include "rss/net/ReadBackpressureController.h"

namespace {

using rss::net::ReadBackpressureController;
using rss::net::ReadTransition;

TEST(ReadBackpressureControllerTest, TransitionsOnlyAtConfiguredBoundaries) {
  ReadBackpressureController controller(3, 1);

  EXPECT_FALSE(controller.paused());
  EXPECT_EQ(controller.onInboundSize(2), ReadTransition::None);
  EXPECT_EQ(controller.onInboundSize(3), ReadTransition::Pause);
  EXPECT_TRUE(controller.paused());
  EXPECT_EQ(controller.onInboundSize(3), ReadTransition::None);
  EXPECT_EQ(controller.onCapacityAvailable(2), ReadTransition::None);
  EXPECT_EQ(controller.onCapacityAvailable(1), ReadTransition::Resume);
  EXPECT_FALSE(controller.paused());
  EXPECT_EQ(controller.onCapacityAvailable(1), ReadTransition::None);
}

}  // namespace
