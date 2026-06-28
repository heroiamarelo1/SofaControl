#pragma once

// Controller-driven file/folder search overlay.

#include <Windows.h>

#include <functional>
#include <string>
#include <vector>

struct SearchOverlayInput {
    float stickY = 0.0f;
    bool navUp = false;
    bool navDown = false;
    bool open = false;
    bool close = false;
};

class SearchOverlay {
public:
    bool Initialize(HINSTANCE instance);
    void SetOwnerWindow(HWND ownerWindow);

    bool Toggle();
    void Close();
    bool IsOpen() const { return isOpen_; }
    HWND WindowHandle() const { return hwnd_; }
    HWND EditHandle() const { return hwndEdit_; }

    void Update(const SearchOverlayInput& input);
    void PositionForKeyboard(int keyboardHeight, int gapPixels);
    void SetBeforeOpenHandler(std::function<void()> handler) { beforeOpenHandler_ = std::move(handler); }

    const std::wstring& StatusMessage() const { return statusMessage_; }

private:
    static constexpr int kWindowWidth = 720;
    static constexpr int kWindowHeight = 420;
    static constexpr int kMargin = 18;
    static constexpr int kEditHeight = 36;
    static constexpr int kStatusHeight = 24;
    static constexpr float kStickNavThreshold = 0.48f;
    static constexpr UINT kStickRepeatFrames = 7;
    static constexpr UINT kSearchTimerId = 1;
    static constexpr UINT kSearchDebounceMs = 280;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND ownerWindow_ = nullptr;
    HWND hwndEdit_ = nullptr;
    HWND hwndList_ = nullptr;
    HWND hwndStatus_ = nullptr;
    HFONT uiFont_ = nullptr;

    bool isOpen_ = false;
    int stickDirY_ = 0;
    UINT stickRepeatCooldown_ = 0;
    std::wstring lastQuery_;
    std::wstring statusMessage_ = L"Search: closed";
    std::vector<std::wstring> results_;
    std::function<void()> beforeOpenHandler_;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void ShowCentered();
    void ScheduleSearch();
    void RunSearch();
    void ClearResults(const wchar_t* message);
    void FillResults(const std::vector<std::wstring>& results);
    void MoveSelection(int delta);
    void HandleStickNavigation(float stickY);
    void OpenSelected();
    void HideAfterOpen();
    std::wstring CurrentQuery() const;
};
