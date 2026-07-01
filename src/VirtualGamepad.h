#pragma once

// Virtual Xbox 360 pad via ViGEm for XInput passthrough to games.
// Physical input is mirrored; game rumble is forwarded to the physical controller.

#include <Windows.h>
#include <XInput.h>

#include <ViGEm/Client.h>

#include <functional>
#include <string>

class VirtualGamepad {
public:
    using RumbleHandler = std::function<void(UCHAR largeMotor, UCHAR smallMotor)>;

    bool IsDriverAvailable() const;

    bool Initialize();
    void Shutdown();

    bool Connect();
    void Disconnect();

    bool IsConnected() const { return targetConnected_; }

    bool Update(const XINPUT_GAMEPAD& gamepad);
    bool ClearInput();

    void SetRumbleHandler(RumbleHandler handler) { rumbleHandler_ = std::move(handler); }

    const std::wstring& StatusMessage() const { return statusMessage_; }

private:
    PVIGEM_CLIENT client_ = nullptr;
    PVIGEM_TARGET target_ = nullptr;
    bool busConnected_ = false;
    bool targetConnected_ = false;
    RumbleHandler rumbleHandler_;
    std::wstring statusMessage_;

    static void CALLBACK OnRumble(PVIGEM_CLIENT client,
        PVIGEM_TARGET target,
        UCHAR largeMotor,
        UCHAR smallMotor,
        UCHAR ledNumber,
        LPVOID userData);
};
