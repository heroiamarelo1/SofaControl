#pragma once

// Xbox controller input via native Windows XInput.

#include <Windows.h>
#include <XInput.h>

class XboxController {
public:
    bool Update();

    bool IsConnected() const { return connected_; }

    bool IsButtonDown(WORD button) const;
    bool WasButtonJustPressed(WORD button) const;
    bool WasButtonJustReleased(WORD button) const;

    // LT + RT + B + Y — toggle mouse / XInput mode.
    bool IsToggleComboHeld() const;
    bool WasToggleComboJustActivated() const { return toggleComboJustActivated_; }

    // True while LT+RT are held (toggle combo in progress — suppress B click).
    bool IsToggleComboArmed() const;

    // RT held — virtual mouse precision mode (slow cursor).
    bool IsRightTriggerHeld() const;

    // LT held — Alt+Tab long-press (RT must stay off for timer).
    bool IsLeftTriggerHeld() const;

    // Left stick normalized to -1.0 … 1.0 with deadzone applied.
    float LeftStickX() const;
    float LeftStickY() const;

    // Right stick normalized; Y inverted (stick up = positive).
    float RightStickX() const;
    float RightStickY() const;

    const XINPUT_GAMEPAD& Gamepad() const { return currentState_.Gamepad; }

    bool SetVibration(WORD leftMotorSpeed, WORD rightMotorSpeed) const;

    static constexpr DWORD kPhysicalUserIndex = 0;

private:
    XINPUT_STATE currentState_{};
    XINPUT_STATE previousState_{};
    bool connected_ = false;
    bool toggleComboHeldPrev_ = false;
    bool toggleComboJustActivated_ = false;

    static float NormalizeStick(SHORT value);
};
