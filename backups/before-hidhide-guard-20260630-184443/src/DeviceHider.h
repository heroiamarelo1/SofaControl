#pragma once

// Manages the physical Xbox controller visibility via HidHide.
// Mouse mode: controller hidden from all apps except SofaControl.
// XInput mode: controller visible natively so games read real hardware.

#include <string>
#include <vector>

class DeviceHider {
public:
    bool IsDriverAvailable() const;

    // Hide controller from all apps (mouse mode). Sets up blacklist on first call.
    bool Hide();

    // Reveal controller to all apps (XInput mode / pre-arm).
    bool Reveal();

    // Re-hide with freshly discovered device paths (e.g. controller reconnected).
    bool RefreshHidden();

    const std::wstring& StatusMessage() const { return statusMessage_; }

    // Reveal on exit so the controller is usable after SofaControl closes.
    void RestoreOnExit();

    bool IsHiding() const { return hidingActive_; }

private:
    std::wstring statusMessage_;
    std::vector<std::wstring> hiddenPaths_;
    std::vector<std::wstring> pathsToForget_;
    bool hidingActive_ = false;

    std::vector<std::wstring> FindConnectedControllerPaths();
    bool ApplyHide();
    bool ApplyReveal();
};
