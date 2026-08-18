#include <gtest/gtest.h>

#include <string>

#include "EnvironmentInfo.h"

TEST(EnvironmentInfoTest, FormatsStableKeyValueOrder) {
  const rss::tools::EnvironmentInfo info{
      .commit = "abc1234",
      .os = "Linux",
      .kernel = "6.8",
      .cpu = "Example_CPU",
      .compiler = "Clang_18",
      .build_type = "Release",
      .workers = 4,
      .requested_slow_receive_buffer_bytes = 2048,
  };

  EXPECT_EQ(rss::tools::formatEnvironment(info),
            "environment commit=abc1234 os=Linux kernel=6.8 "
            "cpu=Example_CPU compiler=Clang_18 build_type=Release "
            "workers=4 requested_slow_receive_buffer_bytes=2048");
}

TEST(EnvironmentInfoTest, NormalizesTokenSeparatorsInValues) {
  const rss::tools::EnvironmentInfo info{
      .commit = "abc 1234",
      .os = "Linux\tGNU",
      .kernel = "6.8\ncustom",
      .cpu = "Example CPU=Model",
      .compiler = "GNU 11.4.0",
      .build_type = "RelWith DebInfo",
      .workers = 2,
      .requested_slow_receive_buffer_bytes = 1024,
  };

  EXPECT_EQ(rss::tools::formatEnvironment(info),
            "environment commit=abc_1234 os=Linux_GNU kernel=6.8_custom "
            "cpu=Example_CPU_Model compiler=GNU_11.4.0 "
            "build_type=RelWith_DebInfo workers=2 "
            "requested_slow_receive_buffer_bytes=1024");
}

TEST(EnvironmentInfoTest, CollectsRequestedRuntimeSettings) {
  const auto info = rss::tools::collectEnvironmentInfo(6, 4096);

  EXPECT_FALSE(info.commit.empty());
  EXPECT_FALSE(info.os.empty());
  EXPECT_FALSE(info.kernel.empty());
  EXPECT_FALSE(info.cpu.empty());
  EXPECT_NE(info.cpu, "unknown");
  EXPECT_FALSE(info.compiler.empty());
  EXPECT_FALSE(info.build_type.empty());
  EXPECT_EQ(info.workers, 6U);
  EXPECT_EQ(info.requested_slow_receive_buffer_bytes, 4096);
}
