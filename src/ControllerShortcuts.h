#pragma once

// Virtual-mouse shortcuts: LT Alt+Tab, LB/RB browser back/forward, RB media controls.

#include <Windows.h>

class AltTabSwitcher;
class KeyboardInput;
class XboxController;

class ControllerShortcuts {
public:
    explicit ControllerShortcuts(KeyboardInput& keyboardInput, AltTabSwitcher& altTabSwitcher);

    void Reset();

    // Returns true when Alt+Tab was opened this frame.
    bool UpdateLtAltTabHold(const XboxController& controller);

    // RB + right stick zoom, plain right-stick scroll, LB/RB tap shortcuts.
    void UpdateShouldersAndZoom(const XboxController& controller);

    // RB + face buttons / d-pad media controls. Returns true when input was consumed.
    bool UpdateMediaControls(const XboxController& controller);

private:
    static constexpr DWORD kLtAltTabHoldMs = 1000;
    static constexpr DWORD kLbShowDesktopHoldMs = 1000;
    static constexpr float kRightStickThreshold = 0.45f;
    static constexpr float kRightStickNeutralThreshold = 0.28f;
    static constexpr UINT kWheelCooldownFrames = 4;

    void UpdateRightStickScroll(const XboxController& controller, bool rbDown);

    KeyboardInput& keyboardInput_;
    AltTabSwitcher& altTabSwitcher_;

    ULONGLONG ltHoldStartMs_ = 0;
    bool ltAltTabTriggeredThisHold_ = false;

    ULONGLONG lbHoldStartMs_ = 0;
    bool lbWasDown_ = false;
    bool lbShowDesktopTriggeredThisHold_ = false;
    bool rbWasDown_ = false;
    bool rbActionUsedThisHold_ = false;
    UINT wheelCooldown_ = 0;

    bool CanStartLtAltTab(const XboxController& controller) const;
};
