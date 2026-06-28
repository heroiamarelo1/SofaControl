#pragma once

// Alt+Tab window switcher driven by the controller.

#include <Windows.h>

#include <string>

class KeyboardInput;

class AltTabSwitcher {
public:
    explicit AltTabSwitcher(KeyboardInput& keyboardInput);

    bool IsActive() const { return active_; }

    void Open();
    void Cancel();
    void Confirm();

    void Update(
        bool dpadLeftDown,
        bool dpadRightDown,
        bool confirm,
        bool cancel);

    const std::wstring& StatusMessage() const { return statusMessage_; }

private:
    // Typematic-style repeat: a quick tap is one step; holding repeats after an
    // initial delay. Mirrors the integrated keyboard's stick navigation feel.
    static constexpr UINT kInitialRepeatDelayFrames = 45;  // ~720 ms before repeat
    static constexpr UINT kRepeatIntervalFrames = 22;      // ~350 ms per repeat

    KeyboardInput& keyboardInput_;
    bool active_ = false;
    int dpadDir_ = 0;
    UINT dpadRepeatCooldown_ = 0;
    std::wstring statusMessage_ = L"Alt+Tab: hold LT 1s (RT off)";

    void ResetNavigationState();
    void HandleDpadNavigation(bool dpadLeftDown, bool dpadRightDown);
    void StepNavigation(int direction);
};
