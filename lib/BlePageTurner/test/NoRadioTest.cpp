// A build without BLE links NoRadio.cpp: every call answers, nothing ever starts.

#include <gtest/gtest.h>

#include "BlePageTurner.h"
#include "Runtime.h"

TEST(BlePageTurnerNoRadioTest, NothingStartsAndEveryCallAnswers) {
  bleturner::detail::resetForTests();
  // Hosts without the radio may never call begin(): the module answers with its defaults.
  EXPECT_FALSE(bleturner::switchOn());
  EXPECT_TRUE(bleturner::beforeScreenChange());
  EXPECT_TRUE(bleturner::beforeSleep(100));
  EXPECT_FALSE(bleturner::holdsHeap());
  EXPECT_EQ(bleturner::beforeChapterBuild(), bleturner::BuildRelease::NotHeld);
  const bleturner::Status st = bleturner::status();
  EXPECT_FALSE(st.compiledIn);
  EXPECT_FALSE(st.running || st.starting || st.stopping);
}

namespace {
unsigned noRadioHostCalls = 0;
}

TEST(BlePageTurnerNoRadioTest, SavedEnabledPreferenceHasNoRuntimeEffects) {
  bleturner::detail::resetForTests();
  noRadioHostCalls = 0;
  const bleturner::Host host{[] {
                               ++noRadioHostCalls;
                               return bleturner::Heap{0, 0};
                             },
                             [] {
                               ++noRadioHostCalls;
                               return true;
                             },
                             [](bleturner::Action) {
                               ++noRadioHostCalls;
                               return false;
                             },
                             [] {
                               ++noRadioHostCalls;
                               return true;
                             },
                             [] {
                               ++noRadioHostCalls;
                               return false;
                             },
                             [] { ++noRadioHostCalls; },
                             [](bool) { ++noRadioHostCalls; },
                             [](bool, const char*, va_list) { ++noRadioHostCalls; },
                             nullptr};
  bleturner::Config config;
  config.enabled = 1;
  bleturner::begin(host, config);
  bleturner::requestConnect();
  const bleturner::Scene reader{bleturner::Where::Reader, 1, true, false, false, false, false, false};
  EXPECT_FALSE(bleturner::tick(reader));
  EXPECT_FALSE(bleturner::switchOn());
  EXPECT_FALSE(bleturner::detail::startAsync());
  EXPECT_EQ(bleturner::beforeChapterBuild(), bleturner::BuildRelease::NotHeld);
  EXPECT_FALSE(bleturner::holdsHeap());
  EXPECT_EQ(bleturner::why(), bleturner::Why::Off);
  bleturner::afterPaint();
  bleturner::acknowledgeLinkNote();
  EXPECT_EQ(bleturner::linkNote(), bleturner::LinkNote::None);
  EXPECT_TRUE(bleturner::beforeScreenChange());
  EXPECT_TRUE(bleturner::beforeSleep(100));
  EXPECT_TRUE(bleturner::switchOff());
  EXPECT_FALSE(bleturner::status().readerDeferred);
  EXPECT_EQ(noRadioHostCalls, 0u);
  EXPECT_EQ(config.enabled, 1);  // Keep the saved preference for a later BLE-enabled firmware.
  bleturner::detail::resetForTests();
}
