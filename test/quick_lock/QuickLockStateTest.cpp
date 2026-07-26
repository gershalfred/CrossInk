#include <gtest/gtest.h>

#include "src/util/QuickLockState.h"

TEST(QuickLockStateTest, LockTransitionPausesReadingStats) {
  QuickLockState state;

  const auto transition = state.toggle();

  EXPECT_TRUE(state.isLocked());
  EXPECT_EQ(transition, QuickLockState::Transition::PauseReading);
}

TEST(QuickLockStateTest, UnlockTransitionResumesReadingStats) {
  QuickLockState state;
  (void)state.toggle();

  const auto transition = state.toggle();

  EXPECT_FALSE(state.isLocked());
  EXPECT_EQ(transition, QuickLockState::Transition::ResumeReading);
}
