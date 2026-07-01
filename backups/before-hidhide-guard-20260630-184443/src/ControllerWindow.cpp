#include "ControllerWindow.h"

#include "SystemActions.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>

namespace {
// Load the embedded application icon at the requested size, falling back to the
// generic application icon if the resource is missing.
HICON LoadAppIcon(HINSTANCE instance, int cx, int cy) {
    HICON icon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

// Absolute path to a file shipped next to the executable (e.g. assets\foo.png).
std::wstring AssetPath(const wchar_t* relative) {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.erase(slash + 1);
    }
    path += relative;
    return path;
}

HFONT MakeCalibri(int height, int weight) {
    return CreateFontW(
        height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Calibri");
}

void FillRoundRect(HDC hdc, RECT rc, int radius, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, b));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, p));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(b);
    DeleteObject(p);
}
}  // namespace

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {
// Paint the window title bar dark to match the UI (Win10 2004+/Win11).
void EnableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19 /*older builds*/, &dark, sizeof(dark));
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// Command-list popup: a separate top-level window that renders the supplied
// infographic (assets\command_list.png) scaled to fit the screen.
// ---------------------------------------------------------------------------
namespace {

constexpr wchar_t kCmdListClass[] = L"SofaControlCmdList";

LRESULT CALLBACK CmdListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

        HBRUSH bg = CreateSolidBrush(RGB(12, 13, 18));
        FillRect(mem, &rc, bg);
        DeleteObject(bg);

        auto* image = reinterpret_cast<Gdiplus::Image*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (image) {
            Gdiplus::Graphics g(mem);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            g.DrawImage(image, 0, 0, rc.right, rc.bottom);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        DestroyWindow(hwnd);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_NCLBUTTONDOWN:
        // Close on the caption [X] without entering Windows' modal NC-button
        // loop, which would otherwise freeze the virtual mouse until a real
        // mouse release.
        if (wParam == HTCLOSE) {
            DestroyWindow(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE) {
            DestroyWindow(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_NCDESTROY: {
        auto* image = reinterpret_cast<Gdiplus::Image*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        delete image;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

ControllerWindow::ControllerWindow()
    : mouse_(settings_),
      altTabSwitcher_(keyboardInput_),
      controllerShortcuts_(keyboardInput_, altTabSwitcher_) {
}

bool ControllerWindow::Create(HINSTANCE instance) {
    instance_ = instance;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // dark theme is painted in WM_PAINT
    wc.lpszClassName = L"SofaControlMainWnd";
    wc.hIcon = LoadAppIcon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = LoadAppIcon(instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    RegisterClassExW(&wc);

    // Calibri fonts + dark theme brushes shared across the UI.
    uiFont_ = MakeCalibri(-16, FW_NORMAL);
    titleFont_ = MakeCalibri(-30, FW_SEMIBOLD);
    sectionFont_ = MakeCalibri(-14, FW_SEMIBOLD);
    bgBrush_ = CreateSolidBrush(kColBg);
    cardBrush_ = CreateSolidBrush(kColCard);
    appIconLarge_ = LoadAppIcon(instance, 64, 64);

    // Size the window so the client area matches our layout exactly.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, kClientWidth, kClientHeight};
    AdjustWindowRect(&rc, style, FALSE);

    hwnd_ = CreateWindowExW(
        0,
        L"SofaControlMainWnd",
        L"SofaControl",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_) {
        return false;
    }

    EnableDarkTitleBar(hwnd_);

    // Start minimized to the tray instead of the taskbar.
    return true;
}

int ControllerWindow::Run() {
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK ControllerWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ControllerWindow* self = reinterpret_cast<ControllerWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<ControllerWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT ControllerWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        trayIconAdded_ = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;  // painted in WM_PAINT to avoid flicker

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);

        // Double-buffer the chrome so status/mode updates don't flicker.
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));
        DrawDarkUi(mem, client);
        BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, reinterpret_cast<HWND>(lParam) == hwndDriverStatus_ ? kColDim : kColText);
        return reinterpret_cast<LRESULT>(cardBrush_);
    }

    case WM_DRAWITEM:
        OnDrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;

    case WM_MEASUREITEM: {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis->CtlType == ODT_COMBOBOX) {
            mis->itemHeight = 24;
        }
        return TRUE;
    }

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == hwndSlider_) {
            OnHScroll(wParam);
        }
        return 0;

    case WM_COMMAND:
        OnCommand(wParam, lParam);
        return 0;

    case WM_TIMER:
        if (wParam == kTimerId) {
            OnTimer();
        } else if (wParam == kReminderTimerId) {
            CheckDonationReminder();
        }
        return 0;

    case WM_NCLBUTTONDOWN:
        // Handle the caption buttons ourselves so a synthetic (virtual mouse) click
        // does not enter Windows' modal NC-button loop, which would freeze our own
        // input-polling thread until a real mouse release.
        if (wParam == HTMINBUTTON || wParam == HTCLOSE) {
            HideToTray();
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE || (wParam & 0xFFF0) == SC_CLOSE) {
            HideToTray();
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case kTrayCallbackMsg:
        if (LOWORD(lParam) == NIN_BALLOONUSERCLICK) {
            // The user clicked the donation balloon: open the link, never nag again.
            OpenDonateLink();
            config_.SetDonateStage(4);
            config_.Save();
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == WM_LBUTTONUP) {
            ShowFromTray();
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayMenu();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        KillTimer(hwnd, kReminderTimerId);
        RemoveTrayIcon();
        controllerShortcuts_.Reset();
        mouse_.ReleaseAll();
        integratedKeyboard_.Close();
        searchOverlay_.Close();
        virtualGamepad_.Shutdown();
        deviceHider_.RestoreOnExit();
        if (uiFont_) { DeleteObject(uiFont_); uiFont_ = nullptr; }
        if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
        if (sectionFont_) { DeleteObject(sectionFont_); sectionFont_ = nullptr; }
        if (bgBrush_) { DeleteObject(bgBrush_); bgBrush_ = nullptr; }
        if (cardBrush_) { DeleteObject(cardBrush_); cardBrush_ = nullptr; }
        if (gdiplusToken_) { Gdiplus::GdiplusShutdown(gdiplusToken_); gdiplusToken_ = 0; }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void ControllerWindow::OnCreate(HWND hwnd) {
    hwnd_ = hwnd;
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    config_.Load();

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusStartupInput, nullptr);

    preArmChecked_ = config_.PreArmEnabled();
    combosChecked_ = config_.CombosEnabled();
    wakeChecked_ = config_.WakeEnabled();
    startupChecked_ = SystemActions::IsRunAtStartup();

    CreateChildControls(hwnd);
    AddTrayIcon();

    deviceHider_.Hide();
    virtualGamepad_.SetRumbleHandler([this](UCHAR largeMotor, UCHAR smallMotor) {
        const WORD left = static_cast<WORD>(largeMotor) << 8;
        const WORD right = static_cast<WORD>(smallMotor) << 8;
        controller_.SetVibration(left, right);
    });
    virtualGamepad_.Initialize();
    integratedKeyboard_.Initialize(instance_);
    integratedKeyboard_.SetOwnerWindow(hwnd_);
    searchOverlay_.Initialize(instance_);
    searchOverlay_.SetOwnerWindow(hwnd_);
    searchOverlay_.SetBeforeOpenHandler([this]() {
        integratedKeyboard_.Close();
        if (!config_.PreArmEnabled()) {
            return;
        }
        if (config_.GetBackend() == ControllerBackend::Emulated360) {
            return;
        }

        preArmActive_ = true;
        preArmStartMs_ = GetTickCount64();
        preArmLastDisplayedSec_ = ULONGLONG(-1);
        aLastPressMs_ = 0;
        deviceHider_.Reveal();
        UpdateStatusText();
        InvalidateRect(hwnd_, nullptr, FALSE);
    });
    ApplyModeForCurrent();
    UpdatePreArmAvailability();
    UpdateStatusText();
    SetTimer(hwnd, kTimerId, kPollMs, nullptr);

    InitDonationReminder();
    SetTimer(hwnd, kReminderTimerId, kReminderPollMs, nullptr);
    CheckDonationReminder();
}

void ControllerWindow::CreateChildControls(HWND hwnd) {
    const int textX = kMargin + 16;
    const int textW = kClientWidth - 2 * (kMargin + 16);

    hwndModeStatus_ = CreateWindowW(
        L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        textX, kModeStatusY, textW, kModeStatusHeight,
        hwnd, nullptr, instance_, nullptr);

    hwndDriverStatus_ = CreateWindowW(
        L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        textX, kDriverStatusY, textW, kDriverStatusHeight,
        hwnd, nullptr, instance_, nullptr);

    hwndSensitivityLabel_ = CreateWindowW(
        L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        textX, kSensitivityLabelY, textW, kSensitivityLabelHeight,
        hwnd, nullptr, instance_, nullptr);

    hwndSlider_ = CreateWindowW(
        TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        textX, kSliderY, textW, kSliderHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSliderId)), instance_, nullptr);

    SendMessageW(hwndSlider_, TBM_SETRANGE, TRUE,
        MAKELPARAM(AppSettings::kMinSliderPercent, AppSettings::kMaxSliderPercent));
    SendMessageW(hwndSlider_, TBM_SETPOS, TRUE, config_.SensitivityPercent());
    SendMessageW(hwndSlider_, TBM_SETTICFREQ, 25, 0);
    settings_.SetSensitivityMultiplier(config_.SensitivityPercent() / 100.0f);

    // Left column: XInput backend selector.
    hwndBackendLabel_ = CreateWindowW(
        L"STATIC", L"XInput backend (when in game mode):",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        kCol1X + 16, kBackendLabelY, kColWidth - 32, kLabelHeight,
        hwnd, nullptr, instance_, nullptr);

    hwndBackendCombo_ = CreateWindowW(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | CBS_OWNERDRAWFIXED | WS_VSCROLL,
        kCol1X + 16, kBackendComboY, kComboWidth, kComboHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackendComboId)), instance_, nullptr);
    SendMessageW(hwndBackendCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Native Xbox Controller"));
    SendMessageW(hwndBackendCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Emulated 360 Controller"));
    SendMessageW(hwndBackendCombo_, CB_SETCURSEL, static_cast<WPARAM>(config_.GetBackend()), 0);

    // Right column: power action selector.
    hwndSelectLabel_ = CreateWindowW(
        L"STATIC", L"When LT+RT+Select is held (2s):",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        kCol2X + 16, kSelectLabelY, kColWidth - 32, kLabelHeight,
        hwnd, nullptr, instance_, nullptr);

    hwndSelectCombo_ = CreateWindowW(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | CBS_OWNERDRAWFIXED | WS_VSCROLL,
        kCol2X + 16, kSelectComboY, kComboWidth, kComboHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSelectActionComboId)), instance_, nullptr);
    SendMessageW(hwndSelectCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Do nothing"));
    SendMessageW(hwndSelectCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Sleep (suspend)"));
    SendMessageW(hwndSelectCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Shut down"));
    SendMessageW(hwndSelectCombo_, CB_SETCURSEL, static_cast<WPARAM>(config_.GetSelectAction()), 0);

    // Disable visual styles on the combos so our owner-draw dark fill is not
    // overpainted by the themed (white) selection field.
    SetWindowTheme(hwndBackendCombo_, L"", L"");
    SetWindowTheme(hwndSelectCombo_, L"", L"");

    // Owner-drawn checkboxes in two columns (CONTROLLER | SYSTEM).
    const DWORD checkStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    hwndPreArmCheck_ = CreateWindowW(
        L"BUTTON", L"Enable pre-arm (double-tap A reveals)", checkStyle,
        kCol1X + 16, kCheckRow1Y, kCheckWidth, kCheckHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreArmCheckId)), instance_, nullptr);

    hwndWakeCheck_ = CreateWindowW(
        L"BUTTON", L"Allow controller to turn on PC from sleep", checkStyle,
        kCol1X + 16, kCheckRow2Y, kCheckWidth, kCheckHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWakeCheckId)), instance_, nullptr);

    hwndCombosCheck_ = CreateWindowW(
        L"BUTTON", L"Enable system combos (LT+RT+Start / Select)", checkStyle,
        kCol2X + 16, kCheckRow1Y, kCheckWidth, kCheckHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCombosCheckId)), instance_, nullptr);

    hwndStartupCheck_ = CreateWindowW(
        L"BUTTON", L"Turn on when Windows starts", checkStyle,
        kCol2X + 16, kCheckRow2Y, kCheckWidth, kCheckHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartupCheckId)), instance_, nullptr);

    // Bottom button row: Command list | Buy me a coffee | Quit.
    const DWORD btnStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    hwndCommandListBtn_ = CreateWindowW(
        L"BUTTON", L"Command list", btnStyle,
        kMargin, kButtonsY, kButtonWidth, kButtonHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandListBtnId)), instance_, nullptr);

    hwndCoffeeBtn_ = CreateWindowW(
        L"BUTTON", L"Buy me a coffee", btnStyle,
        kMargin + kButtonWidth + kGutter, kButtonsY, kButtonWidth, kButtonHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCoffeeBtnId)), instance_, nullptr);

    hwndQuitBtn_ = CreateWindowW(
        L"BUTTON", L"Quit", btnStyle,
        kMargin + 2 * (kButtonWidth + kGutter), kButtonsY, kButtonWidth, kButtonHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQuitBtnId)), instance_, nullptr);

    // Apply the Calibri font to every child control.
    if (uiFont_) {
        HWND controls[] = {
            hwndModeStatus_, hwndDriverStatus_, hwndSensitivityLabel_, hwndSlider_,
            hwndBackendLabel_, hwndBackendCombo_, hwndSelectLabel_, hwndSelectCombo_,
            hwndPreArmCheck_, hwndCombosCheck_, hwndWakeCheck_, hwndStartupCheck_,
            hwndCommandListBtn_, hwndCoffeeBtn_, hwndQuitBtn_};
        for (HWND control : controls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }
}

void ControllerWindow::ApplyModeForCurrent() {
    if (modeManager_.CurrentMode() == AppMode::Passthrough) {
        mouse_.ReleaseAll();
        controllerShortcuts_.Reset();
        integratedKeyboard_.Close();
        searchOverlay_.Close();
        preArmActive_ = false;
        aLastPressMs_ = 0;

        if (config_.GetBackend() == ControllerBackend::Emulated360) {
            // Real controller stays hidden; the game sees the ViGEm virtual pad.
            deviceHider_.Hide();
            virtualGamepad_.Connect();
        } else {
            // Native: expose the real controller directly to the game.
            virtualGamepad_.Disconnect();
            deviceHider_.Reveal();
        }
    } else {
        controllerShortcuts_.Reset();
        preArmActive_ = false;
        aLastPressMs_ = 0;
        searchOverlay_.Close();
        if (config_.GetBackend() == ControllerBackend::Emulated360) {
            if (virtualGamepad_.Connect()) {
                virtualGamepad_.ClearInput();
            }
        } else {
            virtualGamepad_.Disconnect();
        }
        deviceHider_.Hide();
        PrimeVirtualMouseCursor();
    }
}

HWND ControllerWindow::CaptureModeSwitchForeground() const {
    HWND foreground = GetForegroundWindow();
    if (!foreground || !IsWindow(foreground)) {
        return nullptr;
    }

    HWND root = GetAncestor(foreground, GA_ROOT);
    if (!root) {
        root = foreground;
    }

    const HWND ignored[] = {
        hwnd_,
        integratedKeyboard_.WindowHandle(),
        searchOverlay_.WindowHandle(),
        searchOverlay_.EditHandle(),
    };

    for (HWND ownWindow : ignored) {
        if (!ownWindow) {
            continue;
        }
        if (foreground == ownWindow || root == ownWindow || IsChild(ownWindow, foreground)) {
            return nullptr;
        }
    }

    return root;
}

void ControllerWindow::ScheduleModeSwitchForegroundRestore(HWND target, const RECT* restoreRect) {
    if (!target || !IsWindow(target)) {
        modeSwitchRestoreWindow_ = nullptr;
        modeSwitchRestoreUntilMs_ = 0;
        modeSwitchRestoreRect_ = {};
        modeSwitchRestoreHasRect_ = false;
        return;
    }

    modeSwitchRestoreWindow_ = target;
    modeSwitchRestoreUntilMs_ = GetTickCount64() + kModeSwitchFocusRestoreMs;
    modeSwitchRestoreHasRect_ = restoreRect != nullptr;
    modeSwitchRestoreRect_ = restoreRect ? *restoreRect : RECT{};
}

void ControllerWindow::TickModeSwitchForegroundRestore() {
    if (!modeSwitchRestoreWindow_) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (now > modeSwitchRestoreUntilMs_ || !IsWindow(modeSwitchRestoreWindow_)) {
        modeSwitchRestoreWindow_ = nullptr;
        modeSwitchRestoreUntilMs_ = 0;
        modeSwitchRestoreRect_ = {};
        modeSwitchRestoreHasRect_ = false;
        return;
    }

    HWND foreground = GetForegroundWindow();
    HWND foregroundRoot = foreground ? GetAncestor(foreground, GA_ROOT) : nullptr;
    if (!foregroundRoot) {
        foregroundRoot = foreground;
    }

    bool rectChanged = false;
    RECT currentRect{};
    if (modeSwitchRestoreHasRect_ && GetWindowRect(modeSwitchRestoreWindow_, &currentRect)) {
        constexpr int kRectTolerancePx = 8;
        rectChanged =
            std::abs(currentRect.left - modeSwitchRestoreRect_.left) > kRectTolerancePx ||
            std::abs(currentRect.top - modeSwitchRestoreRect_.top) > kRectTolerancePx ||
            std::abs(currentRect.right - modeSwitchRestoreRect_.right) > kRectTolerancePx ||
            std::abs(currentRect.bottom - modeSwitchRestoreRect_.bottom) > kRectTolerancePx;
    }

    const bool wasIconic = IsIconic(modeSwitchRestoreWindow_) != FALSE;
    if (!wasIconic && !rectChanged && foregroundRoot == modeSwitchRestoreWindow_) {
        modeSwitchRestoreWindow_ = nullptr;
        modeSwitchRestoreUntilMs_ = 0;
        modeSwitchRestoreRect_ = {};
        modeSwitchRestoreHasRect_ = false;
        return;
    }

    if (wasIconic) {
        ShowWindow(modeSwitchRestoreWindow_, SW_RESTORE);
    }
    if (rectChanged) {
        SetWindowPos(
            modeSwitchRestoreWindow_,
            HWND_TOP,
            modeSwitchRestoreRect_.left,
            modeSwitchRestoreRect_.top,
            modeSwitchRestoreRect_.right - modeSwitchRestoreRect_.left,
            modeSwitchRestoreRect_.bottom - modeSwitchRestoreRect_.top,
            SWP_NOOWNERZORDER);
    }
    if (foregroundRoot != modeSwitchRestoreWindow_ || rectChanged || wasIconic) {
        SetForegroundWindow(modeSwitchRestoreWindow_);
    }
}

void ControllerWindow::PrimeVirtualMouseCursor() {
    virtualMouseCursorPrimeFrames_ = kVirtualMouseCursorPrimeFrames;
}

void ControllerWindow::TickVirtualMouseCursorPrime() {
    if (virtualMouseCursorPrimeFrames_ == 0) {
        return;
    }

    --virtualMouseCursorPrimeFrames_;
    ClipCursor(nullptr);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) == 0) {
        for (int i = 0; i < 8 && ShowCursor(TRUE) < 0; ++i) {
        }
    }
}

void ControllerWindow::OnTimer() {
    if (!trayIconAdded_) {
        if (++trayRetryCounter_ >= kTrayRetryFrames) {
            trayRetryCounter_ = 0;
            AddTrayIcon();
        }
    } else {
        trayRetryCounter_ = 0;
    }

    TickModeSwitchForegroundRestore();

    controller_.Update();

    if (controller_.WasToggleComboJustActivated()) {
        const HWND restoreTarget = CaptureModeSwitchForeground();
        RECT restoreRect{};
        const bool hasRestoreRect = restoreTarget && GetWindowRect(restoreTarget, &restoreRect);
        modeManager_.TryToggle();
        modeManager_.PlayToggleSound();
        ApplyModeForCurrent();
        ScheduleModeSwitchForegroundRestore(restoreTarget, hasRestoreRect ? &restoreRect : nullptr);
        UpdateStatusText();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (!controller_.IsConnected()) {
        if (virtualGamepad_.IsConnected()) {
            virtualGamepad_.Disconnect();
            UpdateStatusText();
        }
        mouse_.ReleaseAll();
        return;
    }

    // System combos work in every mode (so a game can be closed while playing).
    const bool systemComboEngaged = HandleSystemCombos();

    // Passthrough (game) mode: nothing to drive for Native; mirror input for Emulated 360.
    if (modeManager_.CurrentMode() == AppMode::Passthrough) {
        mouse_.ReleaseAll();
        if (config_.GetBackend() == ControllerBackend::Emulated360) {
            if (!virtualGamepad_.IsConnected()) {
                virtualGamepad_.Connect();
                UpdateStatusText();
            }
            if (systemComboEngaged) {
                virtualGamepad_.ClearInput();
            } else {
                virtualGamepad_.Update(controller_.Gamepad());
            }
        }
        return;
    }

    // VirtualMouse mode below.
    TickVirtualMouseCursorPrime();

    // While LT+RT is held the controller is driving a system combo; don't also move
    // the mouse or fire LB/RB shortcuts.
    if (systemComboEngaged) {
        mouse_.ReleaseAll();
        return;
    }

    // Tick the pre-arm countdown (reveal → wait for XInput toggle → hide if timeout).
    TickPreArm();

    // Retry hiding if HidHide failed (only when not pre-armed, where reveal is intentional).
    if (!preArmActive_) {
        if (!deviceHider_.IsHiding()) {
            if (++hideRetryCounter_ >= kHideRetryFrames) {
                hideRetryCounter_ = 0;
                deviceHider_.RefreshHidden();
                UpdateStatusText();
            }
        } else {
            hideRetryCounter_ = 0;
        }
    }

    if (altTabSwitcher_.IsActive()) {
        mouse_.ReleaseAll();
        altTabSwitcher_.Update(
            controller_.IsButtonDown(XINPUT_GAMEPAD_DPAD_LEFT),
            controller_.IsButtonDown(XINPUT_GAMEPAD_DPAD_RIGHT),
            controller_.WasButtonJustPressed(XINPUT_GAMEPAD_A),
            controller_.WasButtonJustPressed(XINPUT_GAMEPAD_B));
        if (!altTabSwitcher_.IsActive()) {
            mouse_.ReleaseAll();
            if (controller_.IsButtonDown(XINPUT_GAMEPAD_A)) {
                suppressMouseUntilARelease_ = true;
            }
            if (controller_.IsButtonDown(XINPUT_GAMEPAD_B)) {
                suppressMouseUntilBRelease_ = true;
            }
        }
        return;
    }

    if (controller_.WasButtonJustPressed(XINPUT_GAMEPAD_Y) && searchOverlay_.IsOpen() && integratedKeyboard_.IsOpen()) {
        mouse_.ReleaseAll();
        integratedKeyboard_.Close();
        searchOverlay_.Close();
        UpdateStatusText();
        return;
    }

    if (controller_.WasButtonJustPressed(XINPUT_GAMEPAD_Y) && !integratedKeyboard_.IsOpen()) {
        mouse_.ReleaseAll();
        const bool opened = searchOverlay_.Toggle();
        if (opened) {
            searchOverlay_.PositionForKeyboard(integratedKeyboard_.WindowHeight(), 12);
            if (preArmActive_) {
                preArmActive_ = false;
                preArmLastDisplayedSec_ = ULONGLONG(-1);
            }
            aLastPressMs_ = 0;
            if (!deviceHider_.IsHiding()) {
                deviceHider_.Hide();
            }
            if (!integratedKeyboard_.IsOpen()) {
                integratedKeyboard_.OpenBelowWindow(
                    searchOverlay_.WindowHandle(),
                    searchOverlay_.EditHandle(),
                    12);
            }
        }
        UpdateStatusText();
        return;
    }

    if (controller_.WasButtonJustPressed(XINPUT_GAMEPAD_X)) {
        mouse_.ReleaseAll();
        const bool opened = searchOverlay_.IsOpen() && !integratedKeyboard_.IsOpen()
                                ? integratedKeyboard_.OpenBelowWindow(
                                      searchOverlay_.WindowHandle(),
                                      searchOverlay_.EditHandle(),
                                      12)
                                : integratedKeyboard_.Toggle();
        if (opened) {
            // While the keyboard owns input, the controller must stay hidden from
            // Windows so D-pad/buttons can't leak to the shell. Cancel any pre-arm
            // reveal and re-assert the HidHide cloak.
            if (preArmActive_) {
                preArmActive_ = false;
                preArmLastDisplayedSec_ = ULONGLONG(-1);
            }
            aLastPressMs_ = 0;
            if (!deviceHider_.IsHiding()) {
                deviceHider_.Hide();
            }
        }
        UpdateStatusText();
    }

    if (searchOverlay_.IsOpen() && integratedKeyboard_.IsOpen() &&
        controller_.WasButtonJustPressed(XINPUT_GAMEPAD_START)) {
        mouse_.ReleaseAll();
        integratedKeyboard_.Close();

        SearchOverlayInput searchInput{};
        searchInput.open = true;
        searchOverlay_.Update(searchInput);

        UpdateStatusText();
        return;
    }

    if (integratedKeyboard_.IsOpen()) {
        ControllerKeyboardInput keyboardInput{};
        keyboardInput.stickX = controller_.LeftStickX();
        keyboardInput.stickY = controller_.LeftStickY();
        keyboardInput.rightStickX = controller_.RightStickX();
        keyboardInput.rightStickY = controller_.RightStickY();
        keyboardInput.navUp = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_UP);
        keyboardInput.navDown = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_DOWN);
        keyboardInput.navLeft = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_LEFT);
        keyboardInput.navRight = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_RIGHT);
        keyboardInput.select = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_A);
        keyboardInput.backspace = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_B);
        keyboardInput.space = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_Y);
        keyboardInput.enter = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_START);
        keyboardInput.cursorLeft = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_LEFT_SHOULDER);
        keyboardInput.cursorRight = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_RIGHT_SHOULDER);
        integratedKeyboard_.Update(keyboardInput);
        return;
    }

    if (searchOverlay_.IsOpen()) {
        SearchOverlayInput searchInput{};
        searchInput.stickY = controller_.LeftStickY();
        searchInput.navUp = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_UP);
        searchInput.navDown = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_DPAD_DOWN);
        searchInput.open = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_A) ||
                           controller_.WasButtonJustPressed(XINPUT_GAMEPAD_START);
        searchInput.close = controller_.WasButtonJustPressed(XINPUT_GAMEPAD_B);
        searchOverlay_.Update(searchInput);
        mouse_.ReleaseAll();
        UpdateStatusText();
        return;
    }

    // Detect double-tap A to pre-arm (must be after keyboard/alttab guards).
    CheckPreArmDoubleTap();

    if (controllerShortcuts_.UpdateLtAltTabHold(controller_)) {
        mouse_.ReleaseAll();
        UpdateStatusText();
        return;
    }

    controllerShortcuts_.UpdateShouldersAndZoom(controller_);

    UpdateMouseButtonSuppression();
    mouse_.UpdateButtons(EffectiveLeftMouseDown(), EffectiveRightMouseDown());

    mouse_.MoveFromStick(
        controller_.LeftStickX(),
        controller_.LeftStickY(),
        controller_.IsRightTriggerHeld());
}

void ControllerWindow::CheckPreArmDoubleTap() {
    if (!config_.PreArmEnabled()) return;
    // Pre-arm reveals the *native* controller for a game to detect it. In the
    // Emulated 360 backend the game sees the virtual ViGEm pad instead, so
    // pre-arm makes no sense and must never reveal the real controller.
    if (config_.GetBackend() == ControllerBackend::Emulated360) return;
    if (preArmActive_) return;
    if (suppressMouseUntilARelease_) return;
    if (!controller_.WasButtonJustPressed(XINPUT_GAMEPAD_A)) return;

    const ULONGLONG now = GetTickCount64();
    const bool isDoubleTap = (aLastPressMs_ > 0) && ((now - aLastPressMs_) <= kDoubleTapWindowMs);

    if (isDoubleTap) {
        aLastPressMs_ = 0;
        preArmActive_ = true;
        preArmStartMs_ = now;
        preArmLastDisplayedSec_ = ULONGLONG(-1);
        deviceHider_.Reveal();
        UpdateStatusText();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        aLastPressMs_ = now;
    }
}

void ControllerWindow::TickPreArm() {
    if (!preArmActive_) return;

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsed = now - preArmStartMs_;

    if (elapsed >= kPreArmTimeoutMs) {
        preArmActive_ = false;
        preArmLastDisplayedSec_ = ULONGLONG(-1);
        deviceHider_.Hide();
        UpdateStatusText();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Refresh countdown label once per second.
    const ULONGLONG remainingSec = (kPreArmTimeoutMs - elapsed) / 1000;
    if (remainingSec != preArmLastDisplayedSec_) {
        preArmLastDisplayedSec_ = remainingSec;
        UpdateStatusText();
    }
}

void ControllerWindow::UpdateMouseButtonSuppression() {
    if (suppressMouseUntilARelease_ && !controller_.IsButtonDown(XINPUT_GAMEPAD_A)) {
        suppressMouseUntilARelease_ = false;
    }
    if (suppressMouseUntilBRelease_ && !controller_.IsButtonDown(XINPUT_GAMEPAD_B)) {
        suppressMouseUntilBRelease_ = false;
    }
}

bool ControllerWindow::EffectiveLeftMouseDown() const {
    if (suppressMouseUntilARelease_) {
        return false;
    }
    return controller_.IsButtonDown(XINPUT_GAMEPAD_A);
}

bool ControllerWindow::EffectiveRightMouseDown() const {
    if (suppressMouseUntilBRelease_) {
        return false;
    }
    return controller_.IsButtonDown(XINPUT_GAMEPAD_B) && !controller_.IsToggleComboArmed();
}

void ControllerWindow::OnHScroll(WPARAM wParam) {
    if (LOWORD(wParam) != TB_THUMBTRACK && LOWORD(wParam) != TB_ENDTRACK &&
        LOWORD(wParam) != TB_LINEUP && LOWORD(wParam) != TB_LINEDOWN &&
        LOWORD(wParam) != TB_PAGEUP && LOWORD(wParam) != TB_PAGEDOWN) {
        return;
    }

    const int pos = static_cast<int>(SendMessageW(hwndSlider_, TBM_GETPOS, 0, 0));
    settings_.SetSensitivityMultiplier(static_cast<float>(pos) / 100.0f);
    config_.SetSensitivityPercent(pos);
    config_.Save();
    UpdateSensitivityLabel();
}

void ControllerWindow::OnCommand(WPARAM wParam, LPARAM /*lParam*/) {
    const int id = LOWORD(wParam);
    const int notify = HIWORD(wParam);

    switch (id) {
    case kSelectActionComboId:
        if (notify == CBN_SELCHANGE) {
            OnSelectActionChanged();
        }
        break;
    case kBackendComboId:
        if (notify == CBN_SELCHANGE) {
            OnBackendChanged();
        }
        break;
    case kPreArmCheckId:
        if (notify == BN_CLICKED) {
            OnPreArmToggled();
        }
        break;
    case kCombosCheckId:
        if (notify == BN_CLICKED) {
            OnCombosToggled();
        }
        break;
    case kCoffeeBtnId:
        if (notify == BN_CLICKED) {
            OpenDonateLink();
        }
        break;
    case kWakeCheckId:
        if (notify == BN_CLICKED) {
            OnWakeCheckToggled();
        }
        break;
    case kStartupCheckId:
        if (notify == BN_CLICKED) {
            OnStartupCheckToggled();
        }
        break;
    case kCommandListBtnId:
        if (notify == BN_CLICKED) {
            ShowCommandList();
        }
        break;
    case kQuitBtnId:
        if (notify == BN_CLICKED) {
            DestroyWindow(hwnd_);
        }
        break;
    case kTrayOpenCmd:
        ShowFromTray();
        break;
    case kTrayCommandsCmd:
        ShowCommandList();
        break;
    case kTrayQuitCmd:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

void ControllerWindow::OnSelectActionChanged() {
    const int sel = static_cast<int>(SendMessageW(hwndSelectCombo_, CB_GETCURSEL, 0, 0));
    const SelectAction action = (sel == 1) ? SelectAction::Sleep
                              : (sel == 2) ? SelectAction::Shutdown
                                           : SelectAction::None;
    config_.SetSelectAction(action);
    config_.Save();
}

void ControllerWindow::OnBackendChanged() {
    const int sel = static_cast<int>(SendMessageW(hwndBackendCombo_, CB_GETCURSEL, 0, 0));
    config_.SetBackend(sel == 1 ? ControllerBackend::Emulated360 : ControllerBackend::Native);
    config_.Save();

    // Re-apply so the change takes effect immediately if already in game mode.
    ApplyModeForCurrent();
    UpdatePreArmAvailability();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ControllerWindow::UpdatePreArmAvailability() {
    const bool emulated = config_.GetBackend() == ControllerBackend::Emulated360;
    EnableWindow(hwndPreArmCheck_, emulated ? FALSE : TRUE);

    // Switching to Emulated 360 must cancel any active pre-arm reveal.
    if (emulated && preArmActive_) {
        preArmActive_ = false;
        preArmLastDisplayedSec_ = ULONGLONG(-1);
        if (modeManager_.CurrentMode() == AppMode::VirtualMouse) {
            deviceHider_.Hide();
        }
    }
    InvalidateRect(hwndPreArmCheck_, nullptr, FALSE);
}

void ControllerWindow::OnPreArmToggled() {
    preArmChecked_ = !preArmChecked_;
    config_.SetPreArmEnabled(preArmChecked_);
    config_.Save();
    InvalidateRect(hwndPreArmCheck_, nullptr, FALSE);
    if (!preArmChecked_ && preArmActive_) {
        preArmActive_ = false;
        preArmLastDisplayedSec_ = ULONGLONG(-1);
        if (modeManager_.CurrentMode() == AppMode::VirtualMouse) {
            deviceHider_.Hide();
        }
        UpdateStatusText();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ControllerWindow::OnCombosToggled() {
    combosChecked_ = !combosChecked_;
    config_.SetCombosEnabled(combosChecked_);
    config_.Save();
    InvalidateRect(hwndCombosCheck_, nullptr, FALSE);
}

void ControllerWindow::OpenDonateLink() {
    ShellExecuteW(hwnd_, L"open", kDonateUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

void ControllerWindow::InitDonationReminder() {
    if (config_.FirstRunUnix() == 0) {
        config_.SetFirstRunUnix(static_cast<long long>(time(nullptr)));
        config_.Save();
    }
}

void ControllerWindow::CheckDonationReminder() {
    const int stage = config_.DonateStage();
    if (stage >= 3) {
        return;  // 3 = finished after 45 days, 4 = user already clicked.
    }

    const long long first = config_.FirstRunUnix();
    if (first == 0) {
        return;
    }

    const long long elapsedDays = (static_cast<long long>(time(nullptr)) - first) / (60 * 60 * 24);
    const long long thresholds[] = {14, 30, 45};
    if (stage < 0 || stage > 2) {
        return;
    }

    if (elapsedDays >= thresholds[stage]) {
        ShowDonationBalloon();
        config_.SetDonateStage(stage + 1);
        config_.Save();
    }
}

void ControllerWindow::ShowDonationBalloon() {
    if (!trayIconAdded_) {
        return;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"SofaControl");
    wcscpy_s(nid.szInfo, L"Enjoying the software? Buy me a coffee.");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ControllerWindow::OnWakeCheckToggled() {
    const bool desired = !wakeChecked_;

    std::wstring status;
    const bool ok = SystemActions::SetControllerWake(desired, AppConfig::AppDataDir(), status);
    if (ok) {
        wakeChecked_ = desired;
        config_.SetWakeEnabled(desired);
        config_.Save();
    }
    InvalidateRect(hwndWakeCheck_, nullptr, FALSE);

    MessageBoxW(hwnd_, status.c_str(), L"Controller wake", ok ? MB_ICONINFORMATION : MB_ICONWARNING);
}

void ControllerWindow::OnStartupCheckToggled() {
    const bool desired = !startupChecked_;

    if (SystemActions::SetRunAtStartup(desired)) {
        startupChecked_ = desired;
        config_.SetStartupEnabled(desired);
        config_.Save();
    } else {
        MessageBoxW(hwnd_, L"Could not update the elevated Windows startup task.", L"Startup", MB_ICONWARNING);
    }
    InvalidateRect(hwndStartupCheck_, nullptr, FALSE);
}

bool ControllerWindow::HandleSystemCombos() {
    if (!config_.CombosEnabled()) {
        closeHoldStartMs_ = 0;
        closeTriggered_ = false;
        selectHoldStartMs_ = 0;
        selectTriggered_ = false;
        return false;
    }

    const bool lt    = controller_.IsLeftTriggerHeld();
    const bool rt    = controller_.IsRightTriggerHeld();
    const bool start = controller_.IsButtonDown(XINPUT_GAMEPAD_START);
    const bool back  = controller_.IsButtonDown(XINPUT_GAMEPAD_BACK);
    const bool prefix = lt && rt;
    const ULONGLONG now = GetTickCount64();

    // LT + RT + START held 2 s: close the foreground application.
    if (prefix && start && !back) {
        if (closeHoldStartMs_ == 0) {
            closeHoldStartMs_ = now;
        } else if (!closeTriggered_ && (now - closeHoldStartMs_) >= kCloseAppHoldMs) {
            closeTriggered_ = true;
            MessageBeep(MB_OK);
            SystemActions::CloseForegroundApplication(hwnd_, nullptr);
        }
    } else {
        closeHoldStartMs_ = 0;
        closeTriggered_ = false;
    }

    // LT + RT + SELECT held 2 s: run the configured power action.
    if (prefix && back && !start) {
        if (selectHoldStartMs_ == 0) {
            selectHoldStartMs_ = now;
        } else if (!selectTriggered_ && (now - selectHoldStartMs_) >= kSelectHoldMs) {
            selectTriggered_ = true;
            PerformSelectAction();
        }
    } else {
        selectHoldStartMs_ = 0;
        selectTriggered_ = false;
    }

    return prefix;
}

void ControllerWindow::PerformSelectAction() {
    const SelectAction action = config_.GetSelectAction();
    if (action == SelectAction::None) {
        return;
    }

    MessageBeep(MB_OK);

    switch (action) {
    case SelectAction::Sleep:
        SystemActions::Sleep();
        break;
    case SelectAction::Shutdown:
        SystemActions::Shutdown();
        break;
    default:
        break;
    }
}

void ControllerWindow::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = kTrayCallbackMsg;
    nid.hIcon = LoadAppIcon(instance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wcscpy_s(nid.szTip, L"SofaControl");

    if (trayIconAdded_) {
        trayIconAdded_ = Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
    }
    if (!trayIconAdded_) {
        trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    }

    if (trayIconAdded_) {
        trayRetryCounter_ = 0;
        // Version 4 so balloon clicks arrive as NIN_BALLOONUSERCLICK.
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

void ControllerWindow::RemoveTrayIcon() {
    if (!trayIconAdded_) {
        return;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayIconAdded_ = false;
}

void ControllerWindow::HideToTray() {
    ShowWindow(hwnd_, SW_HIDE);
}

void ControllerWindow::ShowFromTray() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ControllerWindow::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kTrayOpenCmd, L"Open SofaControl");
    AppendMenuW(menu, MF_STRING, kTrayCommandsCmd, L"Command list");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayQuitCmd, L"Quit");

    POINT cursor{};
    GetCursorPos(&cursor);
    // Required so the menu dismisses correctly when clicking elsewhere.
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void ControllerWindow::ShowCommandList() {
    const std::wstring imgPath = AssetPath(L"assets\\command_list.png");

    // Load the infographic; fall back to a short text box if it's missing.
    auto* image = Gdiplus::Image::FromFile(imgPath.c_str());
    if (!image || image->GetLastStatus() != Gdiplus::Ok) {
        delete image;
        MessageBoxW(hwnd_,
            L"Command list image not found (assets\\command_list.png).",
            L"SofaControl — Command list", MB_ICONWARNING | MB_OK);
        return;
    }

    const int imgW = static_cast<int>(image->GetWidth());
    const int imgH = static_cast<int>(image->GetHeight());

    // Scale to fit ~92% of the work area while keeping the aspect ratio.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int maxW = static_cast<int>((work.right - work.left) * 0.92);
    const int maxH = static_cast<int>((work.bottom - work.top) * 0.92);
    double scale = (std::min)(static_cast<double>(maxW) / imgW, static_cast<double>(maxH) / imgH);
    if (scale > 1.0) scale = 1.0;
    const int clientW = static_cast<int>(imgW * scale);
    const int clientH = static_cast<int>(imgH * scale);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CmdListProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kCmdListClass;
        wc.hIcon = LoadAppIcon(instance_, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
        wc.hIconSm = LoadAppIcon(instance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
        RegisterClassExW(&wc);
        registered = true;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, clientW, clientH};
    AdjustWindowRect(&rc, style, FALSE);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;
    const int x = work.left + ((work.right - work.left) - winW) / 2;
    const int y = work.top + ((work.bottom - work.top) - winH) / 2;

    HWND popup = CreateWindowExW(
        0, kCmdListClass, L"SofaControl — Command list", style,
        x, y, winW, winH, hwnd_, nullptr, instance_, image);
    if (!popup) {
        delete image;
        return;
    }
    EnableDarkTitleBar(popup);
    ShowWindow(popup, SW_SHOW);
    UpdateWindow(popup);
}

void ControllerWindow::UpdateSensitivityLabel() {
    const int pos = static_cast<int>(SendMessageW(hwndSlider_, TBM_GETPOS, 0, 0));
    wchar_t buffer[128]{};
    swprintf_s(buffer, L"Virtual mouse sensitivity: %d%% (physical mouse unchanged)", pos);
    SetWindowTextW(hwndSensitivityLabel_, buffer);
}

void ControllerWindow::UpdateStatusText() {
    std::wstring modeLine = L"Active mode: ";
    modeLine += ModeLabel();
    modeLine += controller_.IsConnected() ? L"  |  Controller: connected"
                                          : L"  |  Controller: not connected";
    SetWindowTextW(hwndModeStatus_, modeLine.c_str());
    SetWindowTextW(hwndDriverStatus_, DriverStatusLine().c_str());
    UpdateSensitivityLabel();
}

std::wstring ControllerWindow::DriverStatusLine() const {
    std::wstring line = deviceHider_.StatusMessage();

    if (preArmActive_) {
        const ULONGLONG elapsed = GetTickCount64() - preArmStartMs_;
        const ULONGLONG remaining = (kPreArmTimeoutMs > elapsed) ? (kPreArmTimeoutMs - elapsed) / 1000 : 0;
        wchar_t buf[128]{};
        swprintf_s(buf, L"\r\nPre-arm: controller visible — %llus remaining.\r\nSwitch to XInput to confirm game mode, or wait to re-hide.", remaining);
        line += buf;
        return line;
    }

    if (modeManager_.CurrentMode() == AppMode::Passthrough) {
        if (config_.GetBackend() == ControllerBackend::Emulated360) {
            line += L"\r\n";
            line += virtualGamepad_.StatusMessage();
        }
        return line;
    }

    // Virtual mouse: only the live keyboard state (command hints live in the
    // Command List, not here).
    line += L"\r\n";
    line += searchOverlay_.IsOpen() ? searchOverlay_.StatusMessage() : integratedKeyboard_.StatusMessage();
    return line;
}

std::wstring ControllerWindow::ModeLabel() const {
    switch (modeManager_.CurrentMode()) {
    case AppMode::VirtualMouse:
        return preArmActive_ ? L"Virtual Mouse — pre-arm active" : L"Virtual Mouse";
    case AppMode::Passthrough:
        return config_.GetBackend() == ControllerBackend::Emulated360
                   ? L"Game mode — Emulated Xbox 360"
                   : L"Game mode — Native Xbox controller";
    }
    return L"Unknown";
}

static void DrawTextAt(HDC hdc, int x, int y, const wchar_t* text, COLORREF color) {
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text, static_cast<int>(wcslen(text)));
}

void ControllerWindow::DrawDarkUi(HDC hdc, const RECT& client) const {
    // Background.
    FillRect(hdc, &client, bgBrush_);
    SetBkMode(hdc, TRANSPARENT);

    const bool virtualMode = modeManager_.CurrentMode() == AppMode::VirtualMouse;

    // ---- Banner ----
    RECT banner{ kMargin, kBannerTop, kClientWidth - kMargin, kBannerBottom };
    FillRoundRect(hdc, banner, 18, kColCard, kColCardBorder);

    if (appIconLarge_) {
        DrawIconEx(hdc, banner.left + 18, banner.top + 18, appIconLarge_, 64, 64, 0, nullptr, DI_NORMAL);
    }

    const int textLeft = banner.left + 96;
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, titleFont_));
    DrawTextAt(hdc, textLeft, banner.top + 18, L"SofaControl", kColText);

    SelectObject(hdc, uiFont_);
    DrawTextAt(hdc, textLeft, banner.top + 56,
        L"LT+RT+B+Y: switch mode  \x2022  see Command list for everything", kColDim);

    // Mode badge (top-right).
    const wchar_t* badge = virtualMode
        ? (preArmActive_ ? L"MOUSE (PRE-ARM)" : L"VIRTUAL MOUSE")
        : (config_.GetBackend() == ControllerBackend::Emulated360 ? L"GAME: EMU 360" : L"GAME: NATIVE");
    const COLORREF badgeColor = virtualMode ? kColGreen : kColBlue;
    RECT badgeRect{ banner.right - 184, banner.top + 24, banner.right - 20, banner.top + 60 };
    FillRoundRect(hdc, badgeRect, 12, badgeColor, badgeColor);
    SelectObject(hdc, sectionFont_);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, badge, -1, &badgeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ---- Cards ----
    RECT statusCard{ kMargin, kStatusCardTop, kClientWidth - kMargin, kStatusCardBottom };
    FillRoundRect(hdc, statusCard, 14, kColCard, kColCardBorder);

    RECT backendCard{ kCol1X, kComboCardTop, kCol1X + kColWidth, kComboCardBottom };
    FillRoundRect(hdc, backendCard, 14, kColCard, kColCardBorder);
    RECT selectCard{ kCol2X, kComboCardTop, kCol2X + kColWidth, kComboCardBottom };
    FillRoundRect(hdc, selectCard, 14, kColCard, kColCardBorder);

    RECT controllerCard{ kCol1X, kCheckCardTop, kCol1X + kColWidth, kCheckCardBottom };
    FillRoundRect(hdc, controllerCard, 14, kColCard, kColCardBorder);
    RECT systemCard{ kCol2X, kCheckCardTop, kCol2X + kColWidth, kCheckCardBottom };
    FillRoundRect(hdc, systemCard, 14, kColCard, kColCardBorder);

    // Section titles.
    SelectObject(hdc, sectionFont_);
    DrawTextAt(hdc, kCol1X + 16, kSectionTitleY, L"CONTROLLER", kColBlue);
    DrawTextAt(hdc, kCol2X + 16, kSectionTitleY, L"SYSTEM", kColAmber);

    SelectObject(hdc, oldFont);
}

void ControllerWindow::OnDrawItem(const DRAWITEMSTRUCT* dis) const {
    if (!dis) return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    SetBkMode(dc, TRANSPARENT);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, uiFont_));

    if (dis->CtlType == ODT_COMBOBOX) {
        const COLORREF fill = selected ? RGB(54, 88, 156) : kColCard;
        HBRUSH b = CreateSolidBrush(fill);
        FillRect(dc, &rc, b);
        DeleteObject(b);

        if (dis->itemID != static_cast<UINT>(-1)) {
            wchar_t text[128]{};
            SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
            RECT tr = rc;
            tr.left += 8;
            SetTextColor(dc, kColText);
            DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        SelectObject(dc, oldFont);
        return;
    }

    if (dis->CtlType == ODT_BUTTON) {
        const int id = static_cast<int>(dis->CtlID);
        const bool isCheck = (id == kPreArmCheckId || id == kCombosCheckId ||
                              id == kWakeCheckId || id == kStartupCheckId);

        wchar_t label[128]{};
        GetWindowTextW(dis->hwndItem, label, 128);

        if (isCheck) {
            // Background blends with the card behind it.
            FillRect(dc, &rc, cardBrush_);

            // Pre-arm is unavailable in the Emulated 360 backend: show it
            // greyed-out and unchecked, and ignore clicks (window disabled).
            const bool dim = (id == kPreArmCheckId) &&
                             (config_.GetBackend() == ControllerBackend::Emulated360);

            bool checked = false;
            if (id == kPreArmCheckId)  checked = preArmChecked_ && !dim;
            if (id == kCombosCheckId)  checked = combosChecked_;
            if (id == kWakeCheckId)    checked = wakeChecked_;
            if (id == kStartupCheckId) checked = startupChecked_;

            const int boxSize = 18;
            const int boxY = rc.top + (rc.bottom - rc.top - boxSize) / 2;
            RECT box{ rc.left, boxY, rc.left + boxSize, boxY + boxSize };
            if (checked) {
                FillRoundRect(dc, box, 5, kColGreen, kColGreen);
                // White check mark.
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                HPEN op = static_cast<HPEN>(SelectObject(dc, pen));
                MoveToEx(dc, box.left + 4, box.top + 9, nullptr);
                LineTo(dc, box.left + 8, box.top + 13);
                LineTo(dc, box.left + 14, box.top + 5);
                SelectObject(dc, op);
                DeleteObject(pen);
            } else {
                FillRoundRect(dc, box, 5, kColCard, dim ? RGB(70, 74, 88) : RGB(110, 116, 134));
            }

            RECT tr = rc;
            tr.left = box.right + 10;
            SetTextColor(dc, dim ? kColDim : kColText);
            DrawTextW(dc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            if (dis->itemState & ODS_FOCUS) {
                RECT fr = rc; fr.left = box.right + 8;
                DrawFocusRect(dc, &fr);
            }
            SelectObject(dc, oldFont);
            return;
        }

        // Push button (Command list / Buy me a coffee / Quit).
        COLORREF border = kColCardBorder;
        if (id == kCoffeeBtnId) border = kColAmber;
        else if (id == kQuitBtnId) border = kColRed;
        else if (id == kCommandListBtnId) border = kColBlue;
        const bool pressed = selected;
        const COLORREF face = pressed ? RGB(44, 47, 62) : RGB(36, 39, 52);
        FillRoundRect(dc, rc, 12, face, border);

        SetTextColor(dc, kColText);
        DrawTextW(dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (dis->itemState & ODS_FOCUS) {
            RECT fr = rc; InflateRect(&fr, -4, -4);
            DrawFocusRect(dc, &fr);
        }
    }
    SelectObject(dc, oldFont);
}
