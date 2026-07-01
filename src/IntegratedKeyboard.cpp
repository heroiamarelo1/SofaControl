#include "IntegratedKeyboard.h"

#include <cwctype>
#include <cstring>

namespace {

constexpr wchar_t kWindowClassName[] = L"SofaControlIntegratedKeyboard";

IntegratedKeyboard* KeyboardFromHwnd(HWND hwnd) {
    return reinterpret_cast<IntegratedKeyboard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

int WrapRow(int row, int rowCount) {
    if (row < 0) {
        return rowCount - 1;
    }
    if (row >= rowCount) {
        return 0;
    }
    return row;
}

}  // namespace

void IntegratedKeyboard::BuildLayout() {
    grid_.assign(kGridRows, std::vector<KeyCell>(kGridCols));

    auto setChar = [this](int row, int col, wchar_t character) {
        KeyCell& cell = grid_[static_cast<size_t>(row)][static_cast<size_t>(col)];
        wchar_t label[2]{character, L'\0'};
        wcsncpy_s(cell.label, label, _TRUNCATE);
        cell.character = character;
        cell.kind = KeyKind::Character;
        cell.colSpan = 1;
    };

    auto setAction = [this](int row, int col, const wchar_t* label, KeyKind kind, int colSpan = 1) {
        KeyCell& cell = grid_[static_cast<size_t>(row)][static_cast<size_t>(col)];
        wcsncpy_s(cell.label, label, _TRUNCATE);
        cell.kind = kind;
        cell.colSpan = colSpan;
    };

    const wchar_t* row0 = L"1234567890";
    for (int col = 0; col < 10; ++col) {
        setChar(0, col, row0[col]);
    }

    const wchar_t* row1 = L"qwertyuiop";
    for (int col = 0; col < 10; ++col) {
        setChar(1, col, row1[col]);
    }

    const wchar_t* row2 = L"asdfghjkl";
    for (int col = 0; col < 9; ++col) {
        setChar(2, col, row2[col]);
    }
    setAction(2, 9, L"Del", KeyKind::Backspace);

    setAction(3, 0, L"Shift", KeyKind::Shift);
    const wchar_t* row3 = L"zxcvbnm,.";
    for (int col = 0; col < 9; ++col) {
        setChar(3, col + 1, row3[col]);
    }

    const wchar_t* row4 = L"@#$-_!?";
    for (int col = 0; col < 8; ++col) {
        setChar(4, col, row4[col]);
    }
    setChar(4, 8, L'/');
    grid_[4][8].colSpan = 2;

    setAction(5, 0, L"Space", KeyKind::Space, 5);
    setAction(5, 5, L"Enter", KeyKind::Enter, 5);
}

bool IntegratedKeyboard::Initialize(HINSTANCE instance) {
    instance_ = instance;
    BuildLayout();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&windowClass)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWindowClassName,
        L"SofaControl Keyboard",
        WS_POPUP | WS_BORDER,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance_,
        this);

    return hwnd_ != nullptr;
}

void IntegratedKeyboard::SetOwnerWindow(HWND ownerWindow) {
    ownerWindow_ = ownerWindow;
}

int IntegratedKeyboard::WindowHeight() const {
    return kWindowHeight;
}

bool IntegratedKeyboard::IsValidCell(int row, int col) const {
    if (row < 0 || row >= kGridRows || col < 0 || col >= kGridCols) {
        return false;
    }
    return grid_[static_cast<size_t>(row)][static_cast<size_t>(col)].kind != KeyKind::Empty;
}

int IntegratedKeyboard::FindNearestColInRow(int row, int preferredCol) const {
    if (row < 0 || row >= kGridRows) {
        return -1;
    }

    int bestCol = -1;
    int bestDistance = 1000;

    for (int col = 0; col < kGridCols; ++col) {
        if (!IsValidCell(row, col)) {
            continue;
        }

        const KeyCell& key = grid_[static_cast<size_t>(row)][static_cast<size_t>(col)];
        const int anchorCol = col;
        const int centerCol = col + ((key.colSpan > 1 ? key.colSpan : 1) - 1) / 2;
        const int distance = abs(preferredCol - centerCol);

        if (distance < bestDistance ||
            (distance == bestDistance && abs(preferredCol - anchorCol) < abs(preferredCol - bestCol))) {
            bestDistance = distance;
            bestCol = col;
        }
    }

    return bestCol;
}

void IntegratedKeyboard::EnsureSelectionValid() {
    auto ensure = [this](NavigationFinger finger) {
        if (IsValidCell(SelectedRow(finger), SelectedCol(finger))) {
            return;
        }

        for (int row = 0; row < kGridRows; ++row) {
            for (int col = 0; col < kGridCols; ++col) {
                if (IsValidCell(row, col)) {
                    SetSelection(row, col, finger);
                    return;
                }
            }
        }
    };

    ensure(NavigationFinger::LeftStick);
    ensure(NavigationFinger::RightStick);
}

int IntegratedKeyboard::SelectedRow(NavigationFinger finger) const {
    return finger == NavigationFinger::RightStick ? rightSelectedRow_ : leftSelectedRow_;
}

int IntegratedKeyboard::SelectedCol(NavigationFinger finger) const {
    return finger == NavigationFinger::RightStick ? rightSelectedCol_ : leftSelectedCol_;
}

void IntegratedKeyboard::SetSelection(int row, int col, NavigationFinger finger) {
    if (!IsValidCell(row, col)) {
        return;
    }

    const int currentRow = SelectedRow(finger);
    const int currentCol = SelectedCol(finger);
    if (row == currentRow && col == currentCol) {
        return;
    }

    if (finger == NavigationFinger::RightStick) {
        rightSelectedRow_ = row;
        rightSelectedCol_ = col;
    } else {
        leftSelectedRow_ = row;
        leftSelectedCol_ = col;
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool IntegratedKeyboard::MoveHorizontal(int deltaCol, NavigationFinger finger) {
    if (deltaCol == 0) {
        return false;
    }

    const int row = SelectedRow(finger);
    int col = SelectedCol(finger);

    for (int attempt = 0; attempt < kGridCols; ++attempt) {
        col += deltaCol;
        if (col < 0) {
            col = kGridCols - 1;
        } else if (col >= kGridCols) {
            col = 0;
        }

        if (IsValidCell(row, col)) {
            SetSelection(row, col, finger);
            return true;
        }
    }

    return false;
}

bool IntegratedKeyboard::MoveVertical(int deltaRow, NavigationFinger finger) {
    if (deltaRow == 0) {
        return false;
    }

    const int currentRow = SelectedRow(finger);
    const int currentCol = SelectedCol(finger);
    const int targetRow = WrapRow(currentRow + deltaRow, kGridRows);
    const int targetCol = FindNearestColInRow(targetRow, currentCol);
    if (targetCol < 0) {
        return false;
    }

    if (targetRow == currentRow && targetCol == currentCol) {
        return false;
    }

    SetSelection(targetRow, targetCol, finger);
    return true;
}

void IntegratedKeyboard::HandleSingleStickNavigation(float stickX, float stickY, NavigationFinger finger) {
    int dirX = 0;
    int dirY = 0;
    int& storedDirX = finger == NavigationFinger::RightStick ? rightStickDirX_ : stickDirX_;
    int& storedDirY = finger == NavigationFinger::RightStick ? rightStickDirY_ : stickDirY_;
    UINT& repeatCooldown = finger == NavigationFinger::RightStick ? rightStickRepeatCooldown_ : stickRepeatCooldown_;

    if (stickX > kStickNavThreshold) {
        dirX = 1;
    } else if (stickX < -kStickNavThreshold) {
        dirX = -1;
    }

    if (stickY > kStickNavThreshold) {
        dirY = 1;
    } else if (stickY < -kStickNavThreshold) {
        dirY = -1;
    }

    if (dirX == 0 && dirY == 0) {
        storedDirX = 0;
        storedDirY = 0;
        repeatCooldown = 0;
        return;
    }

    int moveX = 0;
    int moveY = 0;
    const float absX = stickX >= 0.0f ? stickX : -stickX;
    const float absY = stickY >= 0.0f ? stickY : -stickY;
    if (absX >= absY) {
        moveX = dirX;
    } else {
        moveY = dirY;
    }

    const bool directionChanged = moveX != storedDirX || moveY != storedDirY;
    if (!directionChanged && repeatCooldown > 0) {
        --repeatCooldown;
        return;
    }

    bool moved = false;
    if (moveX != 0) {
        moved = MoveHorizontal(moveX, finger);
    } else if (moveY != 0) {
        moved = MoveVertical(moveY, finger);
    }

    if (moved) {
        storedDirX = moveX;
        storedDirY = moveY;
        repeatCooldown = kStickRepeatFrames;
    }
}

void IntegratedKeyboard::HandleStickNavigation(float leftStickX, float leftStickY, float rightStickX, float rightStickY) {
    const bool leftActive = (leftStickX > kStickNavThreshold || leftStickX < -kStickNavThreshold ||
                             leftStickY > kStickNavThreshold || leftStickY < -kStickNavThreshold);
    const bool rightActive = (rightStickX > kStickNavThreshold || rightStickX < -kStickNavThreshold ||
                              rightStickY > kStickNavThreshold || rightStickY < -kStickNavThreshold);

    if (leftActive && !leftStickWasActive_) {
        activeFinger_ = NavigationFinger::LeftStick;
    }
    if (rightActive && !rightStickWasActive_) {
        activeFinger_ = NavigationFinger::RightStick;
    }

    leftStickWasActive_ = leftActive;
    rightStickWasActive_ = rightActive;

    HandleSingleStickNavigation(leftStickX, leftStickY, NavigationFinger::LeftStick);
    HandleSingleStickNavigation(rightStickX, rightStickY, NavigationFinger::RightStick);
}

void IntegratedKeyboard::HandleNavigation(const ControllerKeyboardInput& input) {
    if (input.navUp) {
        MoveVertical(-1, activeFinger_);
        return;
    }
    if (input.navDown) {
        MoveVertical(1, activeFinger_);
        return;
    }
    if (input.navLeft) {
        MoveHorizontal(-1, activeFinger_);
        return;
    }
    if (input.navRight) {
        MoveHorizontal(1, activeFinger_);
        return;
    }

    HandleStickNavigation(input.stickX, input.stickY, input.rightStickX, input.rightStickY);
}

void IntegratedKeyboard::RestoreTypingFocus() {
    if (savedFocusWindow_ && IsWindow(savedFocusWindow_) && savedFocusWindow_ != hwnd_) {
        SetForegroundWindow(savedFocusWindow_);
    }
}

void IntegratedKeyboard::ActivateSelectedKey() {
    const int selectedRow = SelectedRow(activeFinger_);
    const int selectedCol = SelectedCol(activeFinger_);
    if (!IsValidCell(selectedRow, selectedCol)) {
        return;
    }

    const KeyCell& key = grid_[static_cast<size_t>(selectedRow)][static_cast<size_t>(selectedCol)];

    RestoreTypingFocus();

    switch (key.kind) {
    case KeyKind::Character:
        keyboardInput_.TypeCharacter(key.character, shiftActive_);
        shiftActive_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case KeyKind::Backspace:
        keyboardInput_.Backspace();
        break;
    case KeyKind::Enter:
        keyboardInput_.Enter();
        break;
    case KeyKind::Space:
        keyboardInput_.Space();
        break;
    case KeyKind::Shift:
        shiftActive_ = !shiftActive_;
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    default:
        break;
    }
}

void IntegratedKeyboard::ShowBelowCursor() {
    POINT cursor{};
    GetCursorPos(&cursor);

    RECT windowRect{};
    GetWindowRect(hwnd_, &windowRect);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;

    int left = cursor.x - (width / 2);
    int top = cursor.y + kGapBelowCursor;

    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    RECT work{};
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        work = monitorInfo.rcWork;
        if (left < work.left) {
            left = work.left;
        }
        if (left + width > work.right) {
            left = work.right - width;
        }
        if (top + height > work.bottom) {
            top = cursor.y - height - kGapBelowCursor;
        }
        if (top < work.top) {
            top = work.top;
        }
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, left, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void IntegratedKeyboard::ShowBelowRect(const RECT& anchorRect, int gapPixels) {
    RECT windowRect{};
    GetWindowRect(hwnd_, &windowRect);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;

    int left = anchorRect.left + ((anchorRect.right - anchorRect.left) - width) / 2;
    int top = anchorRect.bottom + gapPixels;

    POINT monitorPoint{
        anchorRect.left + ((anchorRect.right - anchorRect.left) / 2),
        anchorRect.top + ((anchorRect.bottom - anchorRect.top) / 2),
    };
    HMONITOR monitor = MonitorFromPoint(monitorPoint, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        const RECT& work = monitorInfo.rcWork;
        if (left < work.left) {
            left = work.left;
        }
        if (left + width > work.right) {
            left = work.right - width;
        }
        if (top + height > work.bottom) {
            top = work.bottom - height;
        }
        if (top < work.top) {
            top = work.top;
        }
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, left, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void IntegratedKeyboard::ResetOpenState() {
    leftSelectedRow_ = 1;
    leftSelectedCol_ = 0;
    rightSelectedRow_ = 1;
    rightSelectedCol_ = 5;
    activeFinger_ = NavigationFinger::LeftStick;
    shiftActive_ = false;
    stickDirX_ = 0;
    stickDirY_ = 0;
    stickRepeatCooldown_ = 0;
    rightStickDirX_ = 0;
    rightStickDirY_ = 0;
    rightStickRepeatCooldown_ = 0;
    leftStickWasActive_ = false;
    rightStickWasActive_ = false;
    EnsureSelectionValid();
}

bool IntegratedKeyboard::Toggle() {
    if (isOpen_) {
        Close();
        return false;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground && foreground != hwnd_ && foreground != ownerWindow_) {
        savedFocusWindow_ = foreground;
    } else {
        savedFocusWindow_ = nullptr;
    }

    ResetOpenState();

    ShowBelowCursor();
    isOpen_ = true;
    statusMessage_ =
        L"Keyboard: stick/d-pad move | A type | B backspace | Y space | Start enter | LB/RB cursor | X close";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool IntegratedKeyboard::OpenBelowWindow(HWND anchorWindow, HWND focusWindow, int gapPixels) {
    if (!anchorWindow || !IsWindow(anchorWindow)) {
        return Toggle();
    }

    savedFocusWindow_ = (focusWindow && IsWindow(focusWindow) && focusWindow != hwnd_) ? focusWindow : anchorWindow;
    ResetOpenState();

    RECT anchorRect{};
    GetWindowRect(anchorWindow, &anchorRect);
    ShowBelowRect(anchorRect, gapPixels);
    isOpen_ = true;
    statusMessage_ =
        L"Keyboard: stick/d-pad move | A type | B backspace | Y space | Start enter | LB/RB cursor | X close";
    InvalidateRect(hwnd_, nullptr, FALSE);
    RestoreTypingFocus();
    return true;
}

void IntegratedKeyboard::Close() {
    if (!hwnd_) {
        isOpen_ = false;
        statusMessage_ = L"Keyboard: closed";
        return;
    }

    ShowWindow(hwnd_, SW_HIDE);
    isOpen_ = false;
    shiftActive_ = false;
    stickDirX_ = 0;
    stickDirY_ = 0;
    stickRepeatCooldown_ = 0;
    rightStickDirX_ = 0;
    rightStickDirY_ = 0;
    rightStickRepeatCooldown_ = 0;
    leftStickWasActive_ = false;
    rightStickWasActive_ = false;
    activeFinger_ = NavigationFinger::LeftStick;
    statusMessage_ = L"Keyboard: closed";

    RestoreTypingFocus();
}

void IntegratedKeyboard::Update(const ControllerKeyboardInput& input) {
    if (!isOpen_) {
        return;
    }

    HandleNavigation(input);

    if (input.select) {
        ActivateSelectedKey();
    }

    if (input.backspace) {
        RestoreTypingFocus();
        keyboardInput_.Backspace();
    }

    if (input.cursorLeft) {
        RestoreTypingFocus();
        keyboardInput_.ArrowLeft();
    }

    if (input.cursorRight) {
        RestoreTypingFocus();
        keyboardInput_.ArrowRight();
    }

    if (input.space) {
        RestoreTypingFocus();
        keyboardInput_.Space();
    }

    if (input.enter) {
        RestoreTypingFocus();
        keyboardInput_.Enter();
    }
}

RECT IntegratedKeyboard::CellRect(int row, int col) const {
    RECT client{};
    GetClientRect(hwnd_, &client);

    const int margin = 8;
    const int usableWidth = (client.right - client.left) - (margin * 2);
    const int usableHeight = (client.bottom - client.top) - (margin * 2);
    const int cellWidth = usableWidth / kGridCols;
    const int cellHeight = usableHeight / kGridRows;

    const KeyCell& key = grid_[static_cast<size_t>(row)][static_cast<size_t>(col)];
    const int span = key.colSpan > 1 ? key.colSpan : 1;

    RECT rect{};
    rect.left = margin + (col * cellWidth);
    rect.top = margin + (row * cellHeight);
    rect.right = rect.left + (cellWidth * span) - 2;
    rect.bottom = rect.top + cellHeight - 2;
    return rect;
}

void IntegratedKeyboard::PaintKeyLabel(
    HDC hdc,
    const KeyCell& key,
    const RECT& cell,
    bool selected) const {
    wchar_t displayLabel[8]{};
    wcsncpy_s(displayLabel, key.label, _TRUNCATE);

    if (shiftActive_ && key.kind == KeyKind::Character && iswlower(key.character)) {
        displayLabel[0] = towupper(key.character);
        displayLabel[1] = L'\0';
    }

    SetTextColor(hdc, selected ? RGB(255, 255, 255) : RGB(230, 230, 230));
    DrawTextW(hdc, displayLabel, -1, const_cast<RECT*>(&cell), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void IntegratedKeyboard::Paint(HDC hdc) const {
    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH background = CreateSolidBrush(RGB(32, 32, 36));
    FillRect(hdc, &client, background);
    DeleteObject(background);

    SetBkMode(hdc, TRANSPARENT);

    HFONT keyFont = CreateFontW(
        16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, keyFont));

    for (int row = 0; row < kGridRows; ++row) {
        for (int col = 0; col < kGridCols; ++col) {
            const KeyCell& key = grid_[static_cast<size_t>(row)][static_cast<size_t>(col)];
            if (key.kind == KeyKind::Empty) {
                continue;
            }

            const bool selectedByLeft = row == leftSelectedRow_ && col == leftSelectedCol_;
            const bool selectedByRight = row == rightSelectedRow_ && col == rightSelectedCol_;
            const bool selected = selectedByLeft || selectedByRight;
            const bool activeSelected =
                (activeFinger_ == NavigationFinger::LeftStick && selectedByLeft) ||
                (activeFinger_ == NavigationFinger::RightStick && selectedByRight);
            const bool shiftKey = key.kind == KeyKind::Shift && shiftActive_;

            RECT cell = CellRect(row, col);
            COLORREF fillColor = shiftKey ? RGB(90, 70, 20) : RGB(58, 58, 64);
            COLORREF borderColor = RGB(90, 90, 98);
            if (selectedByLeft && selectedByRight) {
                fillColor = activeSelected ? RGB(196, 104, 24) : RGB(128, 88, 70);
                borderColor = RGB(255, 202, 120);
            } else if (selectedByRight) {
                fillColor = activeSelected ? RGB(214, 112, 18) : RGB(150, 82, 24);
                borderColor = activeSelected ? RGB(255, 194, 92) : RGB(220, 144, 62);
            } else if (selectedByLeft) {
                fillColor = activeSelected ? RGB(0, 130, 220) : RGB(36, 92, 146);
                borderColor = activeSelected ? RGB(120, 200, 255) : RGB(74, 148, 210);
            }

            HBRUSH brush = CreateSolidBrush(fillColor);
            HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1, borderColor);
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));

            RoundRect(hdc, cell.left, cell.top, cell.right, cell.bottom, 8, 8);
            PaintKeyLabel(hdc, key, cell, selected);

            if (selectedByLeft && selectedByRight) {
                HBRUSH leftDot = CreateSolidBrush(RGB(120, 200, 255));
                HBRUSH rightDot = CreateSolidBrush(RGB(255, 174, 66));
                RECT leftRect{cell.left + 7, cell.top + 7, cell.left + 15, cell.top + 15};
                RECT rightRect{cell.left + 18, cell.top + 7, cell.left + 26, cell.top + 15};
                FillRect(hdc, &leftRect, leftDot);
                FillRect(hdc, &rightRect, rightDot);
                DeleteObject(leftDot);
                DeleteObject(rightDot);
            }

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(keyFont);
}

LRESULT CALLBACK IntegratedKeyboard::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    IntegratedKeyboard* self = KeyboardFromHwnd(hwnd);

    if (msg == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<IntegratedKeyboard*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT IntegratedKeyboard::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT paintStruct{};
        HDC hdc = BeginPaint(hwnd, &paintStruct);

        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

        Paint(memoryDc);
        BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);

        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);

        EndPaint(hwnd, &paintStruct);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
