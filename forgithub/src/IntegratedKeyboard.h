#pragma once

// Controller-driven on-screen keyboard overlay (same process, no osk.exe).

#include <Windows.h>

#include <string>
#include <vector>

#include "KeyboardInput.h"

struct ControllerKeyboardInput {
    float stickX = 0.0f;
    float stickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;
    bool navUp = false;
    bool navDown = false;
    bool navLeft = false;
    bool navRight = false;
    bool select = false;
    bool backspace = false;
    bool space = false;
    bool enter = false;
    bool cursorLeft = false;
    bool cursorRight = false;
};

class IntegratedKeyboard {
public:
    bool Initialize(HINSTANCE instance);
    void SetOwnerWindow(HWND ownerWindow);

    bool Toggle();
    bool OpenBelowWindow(HWND anchorWindow, HWND focusWindow, int gapPixels);
    void Close();
    bool IsOpen() const { return isOpen_; }
    HWND WindowHandle() const { return hwnd_; }
    int WindowHeight() const;

    void Update(const ControllerKeyboardInput& input);

    const std::wstring& StatusMessage() const { return statusMessage_; }

private:
    enum class KeyKind {
        Empty,
        Character,
        Backspace,
        Enter,
        Space,
        Shift,
    };

    enum class NavigationFinger {
        LeftStick,
        RightStick,
    };

    struct KeyCell {
        wchar_t label[8]{};
        wchar_t character = L'\0';
        KeyKind kind = KeyKind::Empty;
        int colSpan = 1;
    };

    static constexpr int kGridRows = 6;
    static constexpr int kGridCols = 10;
    static constexpr int kWindowWidth = 520;
    static constexpr int kWindowHeight = 268;
    static constexpr int kGapBelowCursor = 14;
    static constexpr float kStickNavThreshold = 0.62f;
    static constexpr UINT kStickRepeatFrames = 7;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND ownerWindow_ = nullptr;
    HWND savedFocusWindow_ = nullptr;

    bool isOpen_ = false;
    bool shiftActive_ = false;
    int leftSelectedRow_ = 0;
    int leftSelectedCol_ = 0;
    int rightSelectedRow_ = 0;
    int rightSelectedCol_ = 0;
    NavigationFinger activeFinger_ = NavigationFinger::LeftStick;
    int stickDirX_ = 0;
    int stickDirY_ = 0;
    UINT stickRepeatCooldown_ = 0;
    int rightStickDirX_ = 0;
    int rightStickDirY_ = 0;
    UINT rightStickRepeatCooldown_ = 0;
    bool leftStickWasActive_ = false;
    bool rightStickWasActive_ = false;

    KeyboardInput keyboardInput_;
    std::wstring statusMessage_ = L"Keyboard: closed";

    std::vector<std::vector<KeyCell>> grid_;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void BuildLayout();
    void ShowBelowCursor();
    void ShowBelowRect(const RECT& anchorRect, int gapPixels);
    void ResetOpenState();
    void RestoreTypingFocus();
    void ActivateSelectedKey();
    void HandleNavigation(const ControllerKeyboardInput& input);
    void HandleStickNavigation(float leftStickX, float leftStickY, float rightStickX, float rightStickY);
    void HandleSingleStickNavigation(float stickX, float stickY, NavigationFinger finger);
    bool MoveHorizontal(int deltaCol, NavigationFinger finger);
    bool MoveVertical(int deltaRow, NavigationFinger finger);
    int FindNearestColInRow(int row, int preferredCol) const;
    bool IsValidCell(int row, int col) const;
    void EnsureSelectionValid();
    void SetSelection(int row, int col, NavigationFinger finger);
    int SelectedRow(NavigationFinger finger) const;
    int SelectedCol(NavigationFinger finger) const;
    void Paint(HDC hdc) const;
    void PaintKeyLabel(HDC hdc, const KeyCell& key, const RECT& cell, bool selected) const;
    RECT CellRect(int row, int col) const;
};
