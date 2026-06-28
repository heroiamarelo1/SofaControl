#include "XboxController.h"

#include <algorithm>
#include <cmath>

static constexpr DWORD kControllerIndex = 0;
static constexpr SHORT kStickDeadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
static constexpr BYTE  kTriggerThreshold = 30;

bool XboxController::Update() {
    previousState_ = currentState_;
    const bool prevCombo = toggleComboHeldPrev_;

    ZeroMemory(&currentState_, sizeof(XINPUT_STATE));
    const DWORD result = XInputGetState(kControllerIndex, &currentState_);

    connected_ = (result == ERROR_SUCCESS);
    toggleComboJustActivated_ = connected_ && IsToggleComboHeld() && !prevCombo;
    toggleComboHeldPrev_ = IsToggleComboHeld();

    return connected_;
}

bool XboxController::IsButtonDown(WORD button) const {
    if (!connected_) {
        return false;
    }
    return (currentState_.Gamepad.wButtons & button) != 0;
}

bool XboxController::WasButtonJustPressed(WORD button) const {
    if (!connected_) {
        return false;
    }
    const WORD prev = previousState_.Gamepad.wButtons;
    const WORD curr = currentState_.Gamepad.wButtons;
    return (curr & button) != 0 && (prev & button) == 0;
}

bool XboxController::WasButtonJustReleased(WORD button) const {
    if (!connected_) {
        return false;
    }
    const WORD prev = previousState_.Gamepad.wButtons;
    const WORD curr = currentState_.Gamepad.wButtons;
    return (prev & button) != 0 && (curr & button) == 0;
}

bool XboxController::IsToggleComboHeld() const {
    if (!connected_) {
        return false;
    }
    const auto& pad = currentState_.Gamepad;
    return pad.bLeftTrigger > kTriggerThreshold &&
           pad.bRightTrigger > kTriggerThreshold &&
           (pad.wButtons & XINPUT_GAMEPAD_B) != 0 &&
           (pad.wButtons & XINPUT_GAMEPAD_Y) != 0;
}

bool XboxController::IsToggleComboArmed() const {
    if (!connected_) {
        return false;
    }
    const auto& pad = currentState_.Gamepad;
    return pad.bLeftTrigger > kTriggerThreshold &&
           pad.bRightTrigger > kTriggerThreshold;
}

bool XboxController::IsRightTriggerHeld() const {
    if (!connected_) {
        return false;
    }
    return currentState_.Gamepad.bRightTrigger > kTriggerThreshold;
}

bool XboxController::IsLeftTriggerHeld() const {
    if (!connected_) {
        return false;
    }
    return currentState_.Gamepad.bLeftTrigger > kTriggerThreshold;
}

float XboxController::NormalizeStick(SHORT value) {
    if (std::abs(value) < kStickDeadzone) {
        return 0.0f;
    }

    const float normalized = static_cast<float>(value) / 32767.0f;
    return std::clamp(normalized, -1.0f, 1.0f);
}

float XboxController::LeftStickX() const {
    if (!connected_) {
        return 0.0f;
    }
    return NormalizeStick(currentState_.Gamepad.sThumbLX);
}

float XboxController::LeftStickY() const {
    if (!connected_) {
        return 0.0f;
    }
    // Invert Y: stick up = cursor up (negative dy on Windows).
    return -NormalizeStick(currentState_.Gamepad.sThumbLY);
}

float XboxController::RightStickX() const {
    if (!connected_) {
        return 0.0f;
    }
    return NormalizeStick(currentState_.Gamepad.sThumbRX);
}

float XboxController::RightStickY() const {
    if (!connected_) {
        return 0.0f;
    }
    return -NormalizeStick(currentState_.Gamepad.sThumbRY);
}

bool XboxController::SetVibration(WORD leftMotorSpeed, WORD rightMotorSpeed) const {
    if (!connected_) {
        return false;
    }

    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = leftMotorSpeed;
    vibration.wRightMotorSpeed = rightMotorSpeed;
    return XInputSetState(kPhysicalUserIndex, &vibration) == ERROR_SUCCESS;
}
