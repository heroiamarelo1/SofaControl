#pragma once

// Controller-driven file/folder search overlay.

#include <Windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SearchOverlayInput {
    float stickY = 0.0f;
    bool navUp = false;
    bool navDown = false;
    bool open = false;
    bool close = false;
};

struct SearchResult {
    std::wstring path;
    std::wstring typeLabel;
    std::wstring folderLabel;
    std::wstring fileLabel;
    HICON icon = nullptr;
};

struct SearchIndexEntry {
    std::wstring path;
    std::wstring lowerPath;
    std::wstring lowerName;
    int priority = 5;
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
    static constexpr UINT kDailyIndexTimerId = 2;
    static constexpr UINT kSearchDebounceMs = 280;
    static constexpr UINT kDailyIndexPollMs = 60 * 1000;
    static constexpr UINT kSearchIndexReadyMsg = WM_APP + 31;
    static constexpr UINT kSearchResultsReadyMsg = WM_APP + 32;
    static constexpr int kResultRowHeight = 42;

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
    std::vector<SearchResult> results_;
    std::function<void()> beforeOpenHandler_;
    std::shared_ptr<const std::vector<SearchIndexEntry>> searchIndex_;
    std::thread searchIndexThread_;
    std::mutex searchIndexMutex_;
    std::atomic_bool searchIndexReady_ = false;
    std::atomic_bool searchIndexRunning_ = false;
    std::atomic_bool stopSearchIndex_ = false;
    std::atomic_int searchGeneration_ = 0;
    ULONGLONG appStartMs_ = 0;
    ULONGLONG lastNoResultRebuildMs_ = 0;
    bool dailyIndexRefreshStarted_ = false;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void ShowCentered();
    void ScheduleSearch();
    void RunSearch();
    void ClearResults(const wchar_t* message);
    void FillResults(const std::vector<SearchResult>& results);
    void MoveSelection(int delta);
    void HandleStickNavigation(float stickY);
    void OpenSelected();
    void HideAfterOpen();
    std::wstring CurrentQuery() const;
    void EnsureSearchIndexStarted();
    void StopSearchIndex();
    void DrawResultItem(const DRAWITEMSTRUCT* dis) const;
    void DestroyResultIcons();
    void BeginAsyncSearch(const std::wstring& query);
    void EnsureVisibleResultIcons();
    void BeginIndexRebuild(bool force);
    void CheckScheduledIndexRefresh();
};
