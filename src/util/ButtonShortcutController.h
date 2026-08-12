#pragma once

#include <cstdint>

#include "QuickLockState.h"

class ButtonShortcutController {
 public:
  enum class ChordAction : uint8_t {
    Screenshot = 0,
    QuickLock = 1,
    NextPage = 2,
    PreviousPage = 3,
    Disabled = 4,
    Sleep = 5,
    PageTurn = 6,
    ToggleBookmark = 7,
    ReadingStats = 8,
    MarkFinished = 9,
    ForceRefresh = 10,
    ToggleFont = 11,
    ToggleGuideDots = 12,
    ToggleBionicReading = 13,
    CyclePageTurn = 14,
    SyncProgress = 15,
    FileTransfer = 16,
    CalibreWireless = 17,
    JoinNetwork = 18,
    CreateHotspot = 19,
    ToggleDarkMode = 20,
    Footnotes = 21,
    FileBrowser = 22,
    CreateClipping = 23,
    LookupWord = 24,
  };
  enum class Event : uint8_t { None, QuickLockChanged, Screenshot, NextPage, PreviousPage, ConfiguredPowerAction };
  struct Result {
    Event event = Event::None;
    bool consumeInput = false;
    ChordAction action = ChordAction::Disabled;
  };

  Result update(const uint32_t nowMs, const bool powerPressed, const bool rightPressed, const bool shortPowerRelease,
                const bool quickLockOnShortPower, const ChordAction action) {
    if (chordActive) {
      if (!powerPressed && !rightPressed) chordActive = false;
      return {Event::None, true};
    }
    if (powerPressed && rightPressed && action != ChordAction::Disabled) {
      chordActive = true;
      if (quickLockState.isLocked() && action != ChordAction::QuickLock) return {Event::None, true};
      switch (action) {
        case ChordAction::Screenshot:
          return {Event::Screenshot, true};
        case ChordAction::QuickLock:
          (void)quickLockState.toggle(nowMs);
          return {Event::QuickLockChanged, true};
        case ChordAction::NextPage:
          return {Event::NextPage, true};
        case ChordAction::PreviousPage:
          return {Event::PreviousPage, true};
        case ChordAction::PageTurn:
          return {Event::NextPage, true};
        case ChordAction::Disabled:
          break;
        default:
          return {Event::ConfiguredPowerAction, true, action};
      }
    }
    if (shortPowerRelease && (quickLockState.isLocked() || quickLockOnShortPower)) {
      (void)quickLockState.toggle(nowMs);
      return {Event::QuickLockChanged, true};
    }
    return {Event::None, quickLockState.isLocked()};
  }
  bool isQuickLocked() const { return quickLockState.isLocked(); }
  void toggleQuickLock(const uint32_t nowMs) { (void)quickLockState.toggle(nowMs); }
  void restoreQuickLock(const uint32_t nowMs) {
    if (!quickLockState.isLocked()) (void)quickLockState.toggle(nowMs);
  }
  bool shouldQuickLockSleep(const uint32_t nowMs, const uint32_t timeoutMs) const {
    return quickLockState.shouldSleep(nowMs, timeoutMs);
  }
  bool isChordActive() const { return chordActive; }

 private:
  QuickLockState quickLockState;
  bool chordActive = false;
};
