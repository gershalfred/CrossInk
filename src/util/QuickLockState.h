#pragma once

class QuickLockState {
 public:
  enum class Transition { PauseReading, ResumeReading };

  Transition toggle() {
    locked = !locked;
    return locked ? Transition::PauseReading : Transition::ResumeReading;
  }

  bool isLocked() const { return locked; }

 private:
  bool locked = false;
};
