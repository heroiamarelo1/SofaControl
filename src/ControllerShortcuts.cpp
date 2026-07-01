#include "ControllerShortcuts.h"

#include "AltTabSwitcher.h"
#include "KeyboardInput.h"
#include "XboxController.h"

#include <cmath>

ControllerShortcuts::ControllerShortcuts(KeyboardInput& keyboardInput, AltTabSwitcher& altTabSwitcher)
    : keyboardInput_(keyboardInput),
      altTabSwitcher_(altTabSwitcher) {
}

void ControllerShortcuts::Reset() {
    ltHoldStartMs_ = 0;
    ltAltTabTriggeredThisHold_ = false;
    lbHoldStartMs_ = 0;
    lbWasDown_ = false;
    lbShowDesktopTriggeredThisHold_ = false;
    rbWasDown_ = false;
    rbActionUsedThisHold_ = false;
    wheelCooldown_ = 0;
    keyboardInput_.ReleaseAllModifiers();
    if (altTabSwitcher_.IsActive()) {
        altTabSwitcher_.Cancel();
    }
}

bool ControllerShortcuts::CanStartLtAltTab(const XboxController& controller) const {
    if (altTabSwitcher_.IsActive()) {
        return false;
    }
    if (!controller.IsLeftTriggerHeld()) {
        return false;
    }
    if (controller.IsRightTriggerHeld()) {
        return false;
    }
    if (controller.IsButtonDown(XINPUT_GAMEPAD_B) || controller.IsButtonDown(XINPUT_GAMEPAD_Y)) {
        return false;
    }
    if (controller.IsToggleComboArmed()) {
        return false;
    }
    return true;
}

bool ControllerShortcuts::UpdateLtAltTabHold(const XboxController& controller) {
    if (!CanStartLtAltTab(controller)) {
        ltHoldStartMs_ = 0;
        ltAltTabTriggeredThisHold_ = false;
        return false;
    }

    const ULONGLONG now = GetTickCount64();
    if (ltHoldStartMs_ == 0) {
        ltHoldStartMs_ = now;
        return false;
    }

    if (ltAltTabTriggeredThisHold_) {
        return false;
    }

    if (now - ltHoldStartMs_ < kLtAltTabHoldMs) {
        return false;
    }

    altTabSwitcher_.Open();
    ltAltTabTriggeredThisHold_ = true;
    return true;
}

void ControllerShortcuts::UpdateRightStickScroll(const XboxController& controller, bool rbDown) {
    const float stickY = controller.RightStickY();
    const float absY = std::abs(stickY);

    if (wheelCooldown_ > 0) {
        --wheelCooldown_;
    }

    if (absY < kRightStickNeutralThreshold) {
        wheelCooldown_ = 0;
        return;
    }

    if (absY < kRightStickThreshold || wheelCooldown_ > 0) {
        return;
    }

    const int direction = stickY > 0.0f ? 1 : -1;
    if (rbDown) {
        rbActionUsedThisHold_ = true;
        keyboardInput_.CtrlWheelScroll(-direction);
    } else {
        keyboardInput_.WheelScroll(-direction);
    }

    wheelCooldown_ = kWheelCooldownFrames;
}

void ControllerShortcuts::UpdateShouldersAndZoom(const XboxController& controller) {
    const bool lbDown = controller.IsButtonDown(XINPUT_GAMEPAD_LEFT_SHOULDER);
    const bool rbDown = controller.IsButtonDown(XINPUT_GAMEPAD_RIGHT_SHOULDER);

    if (lbDown && !lbWasDown_) {
        lbWasDown_ = true;
        lbHoldStartMs_ = GetTickCount64();
        lbShowDesktopTriggeredThisHold_ = false;
    }

    if (rbDown && !rbWasDown_) {
        rbWasDown_ = true;
        rbActionUsedThisHold_ = false;
    }

    UpdateRightStickScroll(controller, rbDown);

    if (lbDown && lbWasDown_ && !lbShowDesktopTriggeredThisHold_) {
        const ULONGLONG now = GetTickCount64();
        if (lbHoldStartMs_ != 0 && now - lbHoldStartMs_ >= kLbShowDesktopHoldMs) {
            keyboardInput_.ShowDesktop();
            lbShowDesktopTriggeredThisHold_ = true;
        }
    }

    if (controller.WasButtonJustReleased(XINPUT_GAMEPAD_LEFT_SHOULDER)) {
        if (lbWasDown_ && !lbShowDesktopTriggeredThisHold_) {
            keyboardInput_.TapCombo(VK_MENU, VK_LEFT);
        }
        lbHoldStartMs_ = 0;
        lbWasDown_ = false;
        lbShowDesktopTriggeredThisHold_ = false;
    } else if (!lbDown) {
        lbHoldStartMs_ = 0;
        lbWasDown_ = false;
        lbShowDesktopTriggeredThisHold_ = false;
    }

    if (controller.WasButtonJustReleased(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
        if (rbWasDown_ && !rbActionUsedThisHold_) {
            keyboardInput_.TapCombo(VK_MENU, VK_RIGHT);
        }
        rbWasDown_ = false;
        rbActionUsedThisHold_ = false;
    } else if (!rbDown) {
        rbWasDown_ = false;
        if (!rbDown) {
            rbActionUsedThisHold_ = false;
        }
    }
}

bool ControllerShortcuts::UpdateMediaControls(const XboxController& controller) {
    if (!controller.IsButtonDown(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
        return false;
    }

    WORD key = 0;
    if (controller.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_UP)) {
        key = VK_VOLUME_UP;
    } else if (controller.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_DOWN)) {
        key = VK_VOLUME_DOWN;
    } else if (controller.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_LEFT) ||
               controller.WasButtonJustPressed(XINPUT_GAMEPAD_X)) {
        key = VK_MEDIA_PREV_TRACK;
    } else if (controller.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_RIGHT) ||
               controller.WasButtonJustPressed(XINPUT_GAMEPAD_Y)) {
        key = VK_MEDIA_NEXT_TRACK;
    } else if (controller.WasButtonJustPressed(XINPUT_GAMEPAD_A)) {
        key = VK_MEDIA_PLAY_PAUSE;
    } else if (controller.WasButtonJustPressed(XINPUT_GAMEPAD_B)) {
        key = VK_VOLUME_MUTE;
    }

    if (key == 0) {
        return false;
    }

    rbWasDown_ = true;
    rbActionUsedThisHold_ = true;
    keyboardInput_.TapKey(key);
    return true;
}
