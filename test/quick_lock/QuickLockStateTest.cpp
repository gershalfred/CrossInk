#include <gtest/gtest.h>

#include "src/util/ButtonShortcutController.h"
#include "src/util/QuickLockState.h"

TEST(QuickLockStateTest, TimeoutUsesWrapSafeArithmetic) {
  QuickLockState state;
  state.toggle(0xFFFFFF00u);
  EXPECT_FALSE(state.shouldSleep(0x00000050u, 0x200u));
  EXPECT_TRUE(state.shouldSleep(0x00000120u, 0x200u));
}

TEST(QuickLockStateTest, ZeroTimeoutMeansNever) {
  QuickLockState state;
  state.toggle(10u);
  EXPECT_FALSE(state.shouldSleep(0xFFFFFFFFu, 0u));
}

TEST(ButtonShortcutControllerTest, RightAloneIsImmediateAndUnconsumed) {
  ButtonShortcutController controller;
  const auto result =
      controller.update(10u, false, true, false, false, ButtonShortcutController::ChordAction::QuickLock);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::None);
  EXPECT_FALSE(result.consumeInput);
}

TEST(ButtonShortcutControllerTest, ChordTriggersOnceAndLatchesUntilReleased) {
  ButtonShortcutController controller;
  auto result = controller.update(10u, true, true, false, false, ButtonShortcutController::ChordAction::NextPage);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::NextPage);
  EXPECT_TRUE(result.consumeInput);

  result = controller.update(20u, true, true, false, false, ButtonShortcutController::ChordAction::NextPage);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::None);
  EXPECT_TRUE(result.consumeInput);

  result = controller.update(30u, false, false, true, false, ButtonShortcutController::ChordAction::NextPage);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::None);
  EXPECT_TRUE(result.consumeInput);
}

TEST(ButtonShortcutControllerTest, LockedStateOnlyAllowsQuickLockChord) {
  ButtonShortcutController controller;
  controller.toggleQuickLock(1u);
  auto result = controller.update(2u, true, true, false, false, ButtonShortcutController::ChordAction::Screenshot);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::None);
  EXPECT_TRUE(result.consumeInput);
  EXPECT_TRUE(controller.isQuickLocked());

  (void)controller.update(3u, false, false, true, false, ButtonShortcutController::ChordAction::Screenshot);
  result = controller.update(4u, true, true, false, false, ButtonShortcutController::ChordAction::QuickLock);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::QuickLockChanged);
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutControllerTest, ShortPowerCanToggleLock) {
  ButtonShortcutController controller;
  auto result = controller.update(10u, false, false, true, true, ButtonShortcutController::ChordAction::Disabled);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::QuickLockChanged);
  EXPECT_TRUE(result.consumeInput);
  EXPECT_TRUE(controller.isQuickLocked());

  // Once locked, a valid short physical Power release always unlocks, even
  // when the normal short-Power setting is not Quick Lock.
  result = controller.update(20u, false, false, true, false, ButtonShortcutController::ChordAction::Disabled);
  EXPECT_EQ(result.event, ButtonShortcutController::Event::QuickLockChanged);
  EXPECT_TRUE(result.consumeInput);
  EXPECT_FALSE(controller.isQuickLocked());
}
