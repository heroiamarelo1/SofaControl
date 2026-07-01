#include "AltTabSwitcher.h"

#include "KeyboardInput.h"

AltTabSwitcher::AltTabSwitcher(KeyboardInput& keyboardInput)
    : keyboardInput_(keyboardInput) {
}

void AltTabSwitcher::ResetNavigationState() {
    dpadDir_ = 0;
    dpadRepeatCooldown_ = 0;
}

void AltTabSwitcher::Open() {
    if (active_) {
        return;
    }

    keyboardInput_.ModifierDown(VK_MENU);
    keyboardInput_.TapKey(VK_TAB);
    active_ = true;
    ResetNavigationState();
    statusMessage_ = L"Alt+Tab: d-pad (tap = 1, hold = repeat) | A confirm | B cancel";
}

void AltTabSwitcher::Cancel() {
    if (!active_) {
        return;
    }

    keyboardInput_.TapKey(VK_ESCAPE);
    keyboardInput_.ModifierUp(VK_MENU);
    active_ = false;
    ResetNavigationState();
    statusMessage_ = L"Alt+Tab: cancelled";
}

void AltTabSwitcher::Confirm() {
    if (!active_) {
        return;
    }

    keyboardInput_.ModifierUp(VK_MENU);
    active_ = false;
    ResetNavigationState();
    statusMessage_ = L"Alt+Tab: confirmed";
}

void AltTabSwitcher::StepNavigation(int direction) {
    // Tab / Shift+Tab is always a single linear step in the Win11 Alt+Tab grid.
    if (direction > 0) {
        keyboardInput_.TapKey(VK_TAB);
    } else {
        keyboardInput_.TapCombo(VK_SHIFT, VK_TAB);
    }
}

void AltTabSwitcher::HandleDpadNavigation(bool dpadLeftDown, bool dpadRightDown) {
    // Mirror the integrated keyboard's stick navigation: fire immediately on a
    // new press, then repeat at a paced cadence while the direction is held.
    int dir = 0;
    if (dpadRightDown && !dpadLeftDown) {
        dir = 1;
    } else if (dpadLeftDown && !dpadRightDown) {
        dir = -1;
    }

    if (dir == 0) {
        dpadDir_ = 0;
        dpadRepeatCooldown_ = 0;
        return;
    }

    const bool directionChanged = dir != dpadDir_;
    if (!directionChanged && dpadRepeatCooldown_ > 0) {
        --dpadRepeatCooldown_;
        return;
    }

    StepNavigation(dir);
    dpadDir_ = dir;
    dpadRepeatCooldown_ = directionChanged ? kInitialRepeatDelayFrames : kRepeatIntervalFrames;
}

void AltTabSwitcher::Update(
    bool dpadLeftDown,
    bool dpadRightDown,
    bool confirm,
    bool cancel) {
    if (!active_) {
        return;
    }

    if (cancel) {
        Cancel();
        return;
    }

    if (confirm) {
        Confirm();
        return;
    }

    HandleDpadNavigation(dpadLeftDown, dpadRightDown);
}
