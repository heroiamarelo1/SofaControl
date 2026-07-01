#pragma once

// Toggles between virtual mouse mode and XInput passthrough.

#include "AppMode.h"

#include <Windows.h>
#include <string>

class ModeManager {
public:
    AppMode CurrentMode() const { return mode_; }

    bool TryToggle();

    void PlayToggleSound() const;

    // Path to toggle.wav next to the exe or under assets/.
    static std::wstring ToggleSoundPath();

private:
    AppMode mode_ = AppMode::VirtualMouse;
};
