#pragma once

// Virtual mouse actions via SendInput (does not change physical mouse settings).

#include <Windows.h>

#include "AppSettings.h"

class MouseInput {
public:
    explicit MouseInput(const AppSettings& settings);

    void MoveFromStick(float stickX, float stickY, bool precisionMode = false);

    void UpdateButtons(bool leftDown, bool rightDown);
    void ReleaseAll();

private:
    const AppSettings& settings_;
    bool leftHeld_ = false;
    bool rightHeld_ = false;

    static constexpr float kStickToPixelScale = 18.0f;
    static constexpr float kPrecisionSpeedFactor = 0.28f;

    void MoveRelative(int deltaX, int deltaY);
    void SendMouseFlags(DWORD flags);
};
