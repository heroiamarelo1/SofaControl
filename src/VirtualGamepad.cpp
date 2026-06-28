#include "VirtualGamepad.h"

#include <cstring>

namespace {

std::wstring ViGEmErrorMessage(VIGEM_ERROR error) {
    switch (error) {
    case VIGEM_ERROR_BUS_NOT_FOUND:
        return L"ViGEm: bus driver not installed (install ViGEmBus from github.com/nefarius/ViGEmBus/releases).";
    case VIGEM_ERROR_BUS_ACCESS_FAILED:
        return L"ViGEm: bus access failed.";
    case VIGEM_ERROR_BUS_ALREADY_CONNECTED:
        return L"ViGEm: bus already connected.";
    case VIGEM_ERROR_NO_FREE_SLOT:
        return L"ViGEm: no free XInput slot for virtual controller.";
    case VIGEM_ERROR_ALREADY_CONNECTED:
        return L"ViGEm: virtual controller already connected.";
    default:
        return L"ViGEm: operation failed (error 0x" + std::to_wstring(static_cast<unsigned>(error)) + L").";
    }
}

XUSB_REPORT ToXusbReport(const XINPUT_GAMEPAD& gamepad) {
    XUSB_REPORT report{};
    std::memcpy(&report, &gamepad, sizeof(XINPUT_GAMEPAD));
    return report;
}

}  // namespace

bool VirtualGamepad::IsDriverAvailable() const {
    if (busConnected_ && client_) {
        return true;
    }

    const PVIGEM_CLIENT probe = vigem_alloc();
    if (!probe) {
        return false;
    }

    const VIGEM_ERROR result = vigem_connect(probe);
    const bool available = VIGEM_SUCCESS(result) || result == VIGEM_ERROR_BUS_ALREADY_CONNECTED;
    vigem_disconnect(probe);
    vigem_free(probe);
    return available;
}

bool VirtualGamepad::Initialize() {
    if (busConnected_ && client_) {
        return true;
    }

    client_ = vigem_alloc();
    if (!client_) {
        statusMessage_ = L"ViGEm: failed to allocate client.";
        return false;
    }

    const VIGEM_ERROR connectResult = vigem_connect(client_);
    if (!VIGEM_SUCCESS(connectResult)) {
        statusMessage_ = ViGEmErrorMessage(connectResult);
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }

    busConnected_ = true;
    statusMessage_ = L"ViGEm: bus ready (virtual pad disconnected).";
    return true;
}

void VirtualGamepad::Shutdown() {
    Disconnect();

    if (client_ && busConnected_) {
        vigem_disconnect(client_);
    }

    if (client_) {
        vigem_free(client_);
    }

    client_ = nullptr;
    busConnected_ = false;
    statusMessage_ = L"ViGEm: shut down.";
}

bool VirtualGamepad::Connect() {
    if (targetConnected_) {
        return true;
    }

    if (!busConnected_ || !client_) {
        if (!Initialize()) {
            return false;
        }
    }

    target_ = vigem_target_x360_alloc();
    if (!target_) {
        statusMessage_ = L"ViGEm: failed to allocate virtual Xbox 360 target.";
        return false;
    }

    const VIGEM_ERROR addResult = vigem_target_add(client_, target_);
    if (!VIGEM_SUCCESS(addResult)) {
        statusMessage_ = ViGEmErrorMessage(addResult);
        vigem_target_free(target_);
        target_ = nullptr;
        return false;
    }

    const VIGEM_ERROR notifyResult =
        vigem_target_x360_register_notification(client_, target_, &VirtualGamepad::OnRumble, this);
    if (!VIGEM_SUCCESS(notifyResult)) {
        statusMessage_ = ViGEmErrorMessage(notifyResult);
        vigem_target_remove(client_, target_);
        vigem_target_free(target_);
        target_ = nullptr;
        return false;
    }

    targetConnected_ = true;
    statusMessage_ = L"ViGEm: virtual Xbox 360 connected (XInput passthrough active).";
    return true;
}

void VirtualGamepad::Disconnect() {
    if (!targetConnected_ || !client_ || !target_) {
        targetConnected_ = false;
        if (busConnected_) {
            statusMessage_ = L"ViGEm: bus ready (virtual pad disconnected).";
        }
        return;
    }

    vigem_target_x360_unregister_notification(target_);
    vigem_target_remove(client_, target_);
    vigem_target_free(target_);
    target_ = nullptr;
    targetConnected_ = false;

    if (rumbleHandler_) {
        rumbleHandler_(0, 0);
    }

    statusMessage_ = L"ViGEm: virtual Xbox 360 disconnected.";
}

bool VirtualGamepad::Update(const XINPUT_GAMEPAD& gamepad) {
    if (!targetConnected_ || !client_ || !target_) {
        return false;
    }

    const XUSB_REPORT report = ToXusbReport(gamepad);
    const VIGEM_ERROR result = vigem_target_x360_update(client_, target_, report);
    if (!VIGEM_SUCCESS(result)) {
        statusMessage_ = ViGEmErrorMessage(result);
        return false;
    }

    return true;
}

bool VirtualGamepad::ClearInput() {
    XINPUT_GAMEPAD neutral{};
    return Update(neutral);
}

void CALLBACK VirtualGamepad::OnRumble(PVIGEM_CLIENT /*client*/,
    PVIGEM_TARGET /*target*/,
    UCHAR largeMotor,
    UCHAR smallMotor,
    UCHAR /*ledNumber*/,
    LPVOID userData) {
    auto* self = static_cast<VirtualGamepad*>(userData);
    if (!self || !self->rumbleHandler_) {
        return;
    }

    self->rumbleHandler_(largeMotor, smallMotor);
}
