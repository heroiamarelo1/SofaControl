#include "MouseInput.h"

#include <Windows.h>
#include <cmath>

MouseInput::MouseInput(const AppSettings& settings)
    : settings_(settings) {
}

void MouseInput::MoveFromStick(float stickX, float stickY, bool precisionMode) {
    if (stickX == 0.0f && stickY == 0.0f) {
        return;
    }

    float speed = settings_.WindowsMouseBaseFactor() * settings_.SensitivityMultiplier();
    if (precisionMode) {
        speed *= kPrecisionSpeedFactor;
    }

    const int dx = static_cast<int>(std::lround(stickX * speed * kStickToPixelScale));
    const int dy = static_cast<int>(std::lround(stickY * speed * kStickToPixelScale));

    MoveRelative(dx, dy);
}

void MouseInput::UpdateButtons(bool leftDown, bool rightDown) {
    if (leftDown && !leftHeld_) {
        SendMouseFlags(MOUSEEVENTF_LEFTDOWN);
        leftHeld_ = true;
    } else if (!leftDown && leftHeld_) {
        SendMouseFlags(MOUSEEVENTF_LEFTUP);
        leftHeld_ = false;
    }

    if (rightDown && !rightHeld_) {
        SendMouseFlags(MOUSEEVENTF_RIGHTDOWN);
        rightHeld_ = true;
    } else if (!rightDown && rightHeld_) {
        SendMouseFlags(MOUSEEVENTF_RIGHTUP);
        rightHeld_ = false;
    }
}

void MouseInput::ReleaseAll() {
    if (leftHeld_) {
        SendMouseFlags(MOUSEEVENTF_LEFTUP);
        leftHeld_ = false;
    }
    if (rightHeld_) {
        SendMouseFlags(MOUSEEVENTF_RIGHTUP);
        rightHeld_ = false;
    }
}

void MouseInput::MoveRelative(int deltaX, int deltaY) {
    if (deltaX == 0 && deltaY == 0) {
        return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = deltaX;
    input.mi.dy = deltaY;
    SendInput(1, &input, sizeof(INPUT));
}

void MouseInput::SendMouseFlags(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    SendInput(1, &input, sizeof(INPUT));
}
