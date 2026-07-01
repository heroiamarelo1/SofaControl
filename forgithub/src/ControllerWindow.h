#pragma once

// Main window: controller diagram, mode/HidHide status, sensitivity slider,
// system-action settings, tray icon.

#include "AltTabSwitcher.h"
#include "AppConfig.h"
#include "AppMode.h"
#include "AppSettings.h"
#include "ControllerShortcuts.h"
#include "DeviceHider.h"
#include "IntegratedKeyboard.h"
#include "KeyboardInput.h"
#include "ModeManager.h"
#include "MouseInput.h"
#include "SearchOverlay.h"
#include "VirtualGamepad.h"
#include "XboxController.h"

#include <Windows.h>
#include <string>

class ControllerWindow {
public:
    ControllerWindow();

    bool Create(HINSTANCE instance);
    int Run();

private:
    static constexpr UINT kTimerId = 1;
    static constexpr UINT kReminderTimerId = 2;
    static constexpr UINT kPollMs = 16;
    static constexpr UINT kReminderPollMs = 60 * 60 * 1000;  // re-check donation reminder hourly
    static constexpr UINT kHideRetryFrames = 120;  // ~2 s at 60 Hz

    // Pre-arm: double-tap A reveals the controller so a game can enumerate it.
    static constexpr ULONGLONG kDoubleTapWindowMs = 500;
    static constexpr ULONGLONG kPreArmTimeoutMs   = 90000;

    // System combos (both require a 2 s hold of LT+RT plus the action button).
    static constexpr ULONGLONG kCloseAppHoldMs = 2000;  // LT+RT+START hold to close app
    static constexpr ULONGLONG kSelectHoldMs   = 2000;  // LT+RT+SELECT hold to run power action

    static constexpr wchar_t kDonateUrl[] = L"https://www.paypal.com/paypalme/heroiamarelo/5EUR";

    // Control / command IDs.
    static constexpr int kSliderId = 1001;
    static constexpr int kSelectActionComboId = 1002;
    static constexpr int kWakeCheckId = 1003;
    static constexpr int kStartupCheckId = 1004;
    static constexpr int kCommandListBtnId = 1005;
    static constexpr int kQuitBtnId = 1006;
    static constexpr int kBackendComboId = 1007;
    static constexpr int kPreArmCheckId = 1008;
    static constexpr int kCombosCheckId = 1009;
    static constexpr int kCoffeeBtnId = 1010;

    // Tray.
    static constexpr UINT kTrayCallbackMsg = WM_APP + 1;
    static constexpr UINT kTrayIconId = 1;
    static constexpr UINT kTrayRetryFrames = 300;  // ~5 s at 60 Hz
    static constexpr ULONGLONG kModeSwitchFocusRestoreMs = 2000;
    static constexpr UINT kVirtualMouseCursorPrimeFrames = 45;  // ~750 ms at 60 Hz
    static constexpr int kTrayOpenCmd = 2001;
    static constexpr int kTrayCommandsCmd = 2002;
    static constexpr int kTrayQuitCmd = 2003;

    // Dark theme palette.
    static constexpr COLORREF kColBg         = RGB(16, 17, 23);
    static constexpr COLORREF kColCard       = RGB(28, 30, 40);
    static constexpr COLORREF kColCardBorder = RGB(48, 51, 66);
    static constexpr COLORREF kColText       = RGB(232, 236, 244);
    static constexpr COLORREF kColDim        = RGB(150, 158, 176);
    static constexpr COLORREF kColGreen      = RGB(30, 184, 112);
    static constexpr COLORREF kColBlue       = RGB(78, 138, 236);
    static constexpr COLORREF kColAmber      = RGB(228, 172, 72);
    static constexpr COLORREF kColRed        = RGB(228, 86, 86);

    // Client layout (pixels). Window is sized to fit this client area exactly.
    static constexpr int kClientWidth = 720;
    static constexpr int kClientHeight = 576;
    static constexpr int kMargin = 20;
    static constexpr int kGutter = 16;
    static constexpr int kColWidth = (kClientWidth - 2 * kMargin - kGutter) / 2;  // 332
    static constexpr int kCol1X = kMargin;                                        // 20
    static constexpr int kCol2X = kMargin + kColWidth + kGutter;                  // 368
    static constexpr int kContentWidth = kClientWidth - (kMargin * 2);

    static constexpr int kBannerTop = 16;
    static constexpr int kBannerBottom = 116;

    // Status card.
    static constexpr int kStatusCardTop = 128;
    static constexpr int kStatusCardBottom = 300;
    static constexpr int kModeStatusY = 144;
    static constexpr int kModeStatusHeight = 22;
    static constexpr int kDriverStatusY = 170;
    static constexpr int kDriverStatusHeight = 64;
    static constexpr int kSensitivityLabelY = 240;
    static constexpr int kSensitivityLabelHeight = 18;
    static constexpr int kSliderY = 262;
    static constexpr int kSliderHeight = 28;

    // Dropdown cards (two columns).
    static constexpr int kComboCardTop = 312;
    static constexpr int kComboCardBottom = 392;
    static constexpr int kLabelHeight = 16;
    static constexpr int kBackendLabelY = 324;
    static constexpr int kBackendComboY = 344;
    static constexpr int kSelectLabelY = 324;
    static constexpr int kSelectComboY = 344;
    static constexpr int kComboWidth = kColWidth - 32;  // 300
    static constexpr int kComboHeight = 240;

    // Checkbox cards (two columns).
    static constexpr int kCheckCardTop = 404;
    static constexpr int kCheckCardBottom = 500;
    static constexpr int kSectionTitleY = 416;
    static constexpr int kCheckRow1Y = 442;
    static constexpr int kCheckRow2Y = 470;
    static constexpr int kCheckHeight = 24;
    static constexpr int kCheckWidth = kColWidth - 24;  // 308

    // Bottom button row.
    static constexpr int kButtonsY = 514;
    static constexpr int kButtonHeight = 42;
    static constexpr int kButtonWidth = (kClientWidth - 2 * kMargin - 2 * kGutter) / 3;  // 216

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT uiFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT sectionFont_ = nullptr;
    HBRUSH bgBrush_ = nullptr;
    HBRUSH cardBrush_ = nullptr;
    HICON appIconLarge_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;

    // Owner-drawn checkboxes carry no built-in check state, so we track our own.
    bool preArmChecked_ = true;
    bool combosChecked_ = true;
    bool wakeChecked_ = false;
    bool startupChecked_ = false;

    HWND hwndSlider_ = nullptr;
    HWND hwndModeStatus_ = nullptr;
    HWND hwndDriverStatus_ = nullptr;
    HWND hwndSensitivityLabel_ = nullptr;
    HWND hwndBackendLabel_ = nullptr;
    HWND hwndBackendCombo_ = nullptr;
    HWND hwndSelectLabel_ = nullptr;
    HWND hwndSelectCombo_ = nullptr;
    HWND hwndPreArmCheck_ = nullptr;
    HWND hwndCombosCheck_ = nullptr;
    HWND hwndWakeCheck_ = nullptr;
    HWND hwndStartupCheck_ = nullptr;
    HWND hwndCommandListBtn_ = nullptr;
    HWND hwndCoffeeBtn_ = nullptr;
    HWND hwndQuitBtn_ = nullptr;

    bool trayIconAdded_ = false;
    UINT taskbarCreatedMsg_ = 0;
    UINT trayRetryCounter_ = 0;
    HWND commandListHwnd_ = nullptr;
    bool commandListHeldOpen_ = false;

    AppConfig config_;
    AppSettings settings_;
    ModeManager modeManager_;
    MouseInput mouse_;
    XboxController controller_;
    DeviceHider deviceHider_;
    VirtualGamepad virtualGamepad_;
    IntegratedKeyboard integratedKeyboard_;
    SearchOverlay searchOverlay_;
    KeyboardInput keyboardInput_;
    AltTabSwitcher altTabSwitcher_;
    ControllerShortcuts controllerShortcuts_;

    UINT hideRetryCounter_ = 0;
    bool suppressMouseUntilARelease_ = false;
    bool suppressMouseUntilBRelease_ = false;

    // Pre-arm state
    bool preArmActive_ = false;
    ULONGLONG preArmStartMs_ = 0;
    ULONGLONG preArmLastDisplayedSec_ = ULONGLONG(-1);
    ULONGLONG aLastPressMs_ = 0;

    // System combo state
    ULONGLONG closeHoldStartMs_ = 0;
    bool closeTriggered_ = false;
    ULONGLONG selectHoldStartMs_ = 0;
    bool selectTriggered_ = false;

    HWND modeSwitchRestoreWindow_ = nullptr;
    ULONGLONG modeSwitchRestoreUntilMs_ = 0;
    RECT modeSwitchRestoreRect_{};
    bool modeSwitchRestoreHasRect_ = false;
    UINT virtualMouseCursorPrimeFrames_ = 0;

    void UpdateMouseButtonSuppression();
    bool EffectiveLeftMouseDown() const;
    bool EffectiveRightMouseDown() const;

    void CheckPreArmDoubleTap();
    void TickPreArm();
    // Returns true while the LT+RT system-combo prefix is held (mouse pipeline yields).
    bool HandleSystemCombos();
    void PerformSelectAction();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate(HWND hwnd);
    void OnTimer();
    void OnHScroll(WPARAM wParam);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    void UpdateStatusText();
    void UpdateSensitivityLabel();
    void DrawDarkUi(HDC hdc, const RECT& clientRect) const;
    void OnDrawItem(const DRAWITEMSTRUCT* dis) const;
    void ApplyModeForCurrent();
    HWND CaptureModeSwitchForeground() const;
    void ScheduleModeSwitchForegroundRestore(HWND target, const RECT* restoreRect);
    void TickModeSwitchForegroundRestore();
    void PrimeVirtualMouseCursor();
    void TickVirtualMouseCursorPrime();

    void CreateChildControls(HWND hwnd);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void HideToTray();
    void ShowFromTray();
    void ShowTrayMenu();
    void ShowCommandList();
    void CloseCommandList();
    void OnWakeCheckToggled();
    void OnStartupCheckToggled();
    void OnSelectActionChanged();
    void OnBackendChanged();
    void OnPreArmToggled();
    // Enable/disable the pre-arm checkbox (disabled in the Emulated 360 backend).
    void UpdatePreArmAvailability();
    void OnCombosToggled();
    void OpenDonateLink();

    // Donation reminder (balloon at 14 / 30 / 45 days; stops after a click).
    void InitDonationReminder();
    void CheckDonationReminder();
    void ShowDonationBalloon();

    std::wstring ModeLabel() const;
    std::wstring DriverStatusLine() const;
};
