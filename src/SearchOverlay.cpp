#include "SearchOverlay.h"

#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <shlobj.h>

namespace {

constexpr wchar_t kWindowClassName[] = L"SofaControlSearchOverlay";
constexpr int kEditId = 2101;
constexpr int kListId = 2102;
constexpr int kMaxResults = 80;
constexpr int kMaxCandidates = 500;
constexpr int kMaxVisited = 90000;

SearchOverlay* SearchFromHwnd(HWND hwnd) {
    return reinterpret_cast<SearchOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

HFONT MakeSegoeUiFont(int height, int weight) {
    return CreateFontW(
        height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

std::wstring ToLower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value;
}

std::vector<std::wstring> QueryTokens(const std::wstring& query) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t ch : ToLower(query)) {
        if (iswspace(ch)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool MatchesQuery(const std::filesystem::path& path, const std::vector<std::wstring>& tokens) {
    const std::wstring name = ToLower(path.filename().wstring());
    const std::wstring full = ToLower(path.wstring());
    for (const auto& token : tokens) {
        if (name.find(token) == std::wstring::npos && full.find(token) == std::wstring::npos) {
            return false;
        }
    }
    return true;
}

void AddExistingRoot(std::vector<std::filesystem::path>& roots, const std::filesystem::path& root) {
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec)) {
        return;
    }

    const std::wstring key = ToLower(root.wstring());
    for (const auto& existing : roots) {
        if (ToLower(existing.wstring()) == key) {
            return;
        }
    }
    roots.push_back(root);
}

std::filesystem::path EnvPath(const wchar_t* name) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return buffer;
}

void AddKnownFolderRoot(std::vector<std::filesystem::path>& roots, const KNOWNFOLDERID& folderId) {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        AddExistingRoot(roots, raw);
        CoTaskMemFree(raw);
    }
}

std::filesystem::path Utf8Path(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        return {};
    }

    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), needed);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

std::vector<std::filesystem::path> SteamLibraryCommonRoots(const std::filesystem::path& steamRoot) {
    std::vector<std::filesystem::path> roots;
    if (steamRoot.empty()) {
        return roots;
    }

    AddExistingRoot(roots, steamRoot / L"steamapps" / L"common");

    const std::filesystem::path libraryFile = steamRoot / L"steamapps" / L"libraryfolders.vdf";
    std::ifstream input(libraryFile);
    if (!input) {
        return roots;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::string marker = "\"path\"";
        const size_t markerPos = line.find(marker);
        if (markerPos == std::string::npos) {
            continue;
        }

        const size_t firstQuote = line.find('"', markerPos + marker.size());
        if (firstQuote == std::string::npos) {
            continue;
        }
        const size_t secondQuote = line.find('"', firstQuote + 1);
        if (secondQuote == std::string::npos) {
            continue;
        }

        std::string pathText = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        for (size_t pos = 0; (pos = pathText.find("\\\\", pos)) != std::string::npos; pos += 1) {
            pathText.replace(pos, 2, "\\");
        }

        AddExistingRoot(roots, Utf8Path(pathText) / L"steamapps" / L"common");
    }

    return roots;
}

std::vector<std::filesystem::path> SearchRoots() {
    std::vector<std::filesystem::path> roots;

    AddKnownFolderRoot(roots, FOLDERID_Desktop);
    AddKnownFolderRoot(roots, FOLDERID_PublicDesktop);
    AddKnownFolderRoot(roots, FOLDERID_StartMenu);
    AddKnownFolderRoot(roots, FOLDERID_CommonStartMenu);
    AddKnownFolderRoot(roots, FOLDERID_Programs);
    AddKnownFolderRoot(roots, FOLDERID_CommonPrograms);

    const std::filesystem::path user = EnvPath(L"USERPROFILE");
    const wchar_t* names[] = {L"Desktop", L"Documents", L"Downloads", L"Pictures", L"Videos", L"Music"};
    for (const wchar_t* name : names) {
        AddExistingRoot(roots, user / name);
        AddExistingRoot(roots, user / L"OneDrive" / name);
    }

    const std::filesystem::path systemDrive = EnvPath(L"SystemDrive");
    const std::filesystem::path programFiles = EnvPath(L"ProgramFiles");
    const std::filesystem::path programFilesX86 = EnvPath(L"ProgramFiles(x86)");

    AddExistingRoot(roots, systemDrive / L"XboxGames");
    AddExistingRoot(roots, systemDrive / L"Games");
    AddExistingRoot(roots, systemDrive / L"Epic Games");
    AddExistingRoot(roots, systemDrive / L"GOG Games");
    AddExistingRoot(roots, programFiles / L"Epic Games");
    AddExistingRoot(roots, programFilesX86 / L"Epic Games");
    AddExistingRoot(roots, programFiles / L"EA Games");
    AddExistingRoot(roots, programFiles / L"Ubisoft" / L"Ubisoft Game Launcher" / L"games");
    AddExistingRoot(roots, programFilesX86 / L"Ubisoft" / L"Ubisoft Game Launcher" / L"games");
    AddExistingRoot(roots, programFiles / L"GOG Galaxy" / L"Games");
    AddExistingRoot(roots, programFilesX86 / L"GOG Galaxy" / L"Games");
    AddExistingRoot(roots, programFiles / L"Riot Games");

    for (const auto& steamRoot : {programFilesX86 / L"Steam", programFiles / L"Steam"}) {
        for (const auto& root : SteamLibraryCommonRoots(steamRoot)) {
            AddExistingRoot(roots, root);
        }
    }

    AddExistingRoot(roots, programFiles);
    AddExistingRoot(roots, programFilesX86);

    return roots;
}

std::wstring DisplayText(const std::wstring& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return L"[Folder] " + path;
    }
    const std::wstring ext = ToLower(std::filesystem::path(path).extension().wstring());
    if (ext == L".lnk" || ext == L".url") {
        return L"[Shortcut] " + path;
    }
    if (ext == L".exe") {
        return L"[App] " + path;
    }
    return L"[File] " + path;
}

bool LooksLikeGamePath(const std::wstring& lowerPath) {
    const wchar_t* markers[] = {
        L"\\steamapps\\common\\",
        L"\\epic games\\",
        L"\\xboxgames\\",
        L"\\gog games\\",
        L"\\gog galaxy\\games\\",
        L"\\ea games\\",
        L"\\ubisoft game launcher\\games\\",
        L"\\riot games\\",
    };

    for (const wchar_t* marker : markers) {
        if (lowerPath.find(marker) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

int ResultPriority(const std::wstring& path) {
    const std::filesystem::path fsPath(path);
    const std::wstring lowerPath = ToLower(path);
    const std::wstring ext = ToLower(fsPath.extension().wstring());
    const bool gamePath = LooksLikeGamePath(lowerPath);

    if (ext == L".lnk" || ext == L".url") {
        return 0;
    }
    if (gamePath && ext == L".exe") {
        return 1;
    }
    if (ext == L".exe") {
        return 2;
    }
    if (gamePath) {
        return 3;
    }

    std::error_code ec;
    if (std::filesystem::is_directory(fsPath, ec)) {
        return 4;
    }
    return 5;
}

std::vector<std::wstring> FindMatches(const std::wstring& query) {
    std::vector<std::wstring> results;
    const auto tokens = QueryTokens(query);
    if (tokens.empty()) {
        return results;
    }

    int visited = 0;
    std::set<std::wstring> seen;
    for (const auto& root : SearchRoots()) {
        if (static_cast<int>(results.size()) >= kMaxCandidates || visited >= kMaxVisited) {
            break;
        }

        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::recursive_directory_iterator end;
        while (!ec && it != end && static_cast<int>(results.size()) < kMaxCandidates && visited < kMaxVisited) {
            const auto path = it->path();
            ++visited;
            if (MatchesQuery(path, tokens)) {
                const std::wstring full = path.wstring();
                if (seen.insert(ToLower(full)).second) {
                    results.push_back(full);
                }
            }
            it.increment(ec);
        }
    }

    std::sort(results.begin(), results.end(), [](const std::wstring& a, const std::wstring& b) {
        const int priorityA = ResultPriority(a);
        const int priorityB = ResultPriority(b);
        if (priorityA != priorityB) {
            return priorityA < priorityB;
        }

        const std::wstring nameA = std::filesystem::path(a).filename().wstring();
        const std::wstring nameB = std::filesystem::path(b).filename().wstring();
        const int nameCmp = _wcsicmp(nameA.c_str(), nameB.c_str());
        if (nameCmp != 0) {
            return nameCmp < 0;
        }
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    if (static_cast<int>(results.size()) > kMaxResults) {
        results.resize(kMaxResults);
    }
    return results;
}

void DrawPanel(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

}  // namespace

bool SearchOverlay::Initialize(HINSTANCE instance) {
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    uiFont_ = MakeSegoeUiFont(-17, FW_NORMAL);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName,
        L"SofaControl Search",
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

void SearchOverlay::SetOwnerWindow(HWND ownerWindow) {
    ownerWindow_ = ownerWindow;
}

void SearchOverlay::CreateControls() {
    if (hwndEdit_) {
        return;
    }

    hwndEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        kMargin,
        56,
        kWindowWidth - (kMargin * 2),
        kEditHeight,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
        instance_,
        nullptr);

    hwndList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL,
        kMargin,
        108,
        kWindowWidth - (kMargin * 2),
        kWindowHeight - 156,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)),
        instance_,
        nullptr);

    hwndStatus_ = CreateWindowW(
        L"STATIC",
        L"Type a file or folder name. X opens the SofaControl keyboard.",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        kMargin,
        kWindowHeight - 38,
        kWindowWidth - (kMargin * 2),
        kStatusHeight,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    if (uiFont_) {
        SendMessageW(hwndEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        SendMessageW(hwndList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        SendMessageW(hwndStatus_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }
}

void SearchOverlay::ShowCentered() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - kWindowWidth) / 2;
    const int y = work.top + ((work.bottom - work.top) - kWindowHeight) / 2;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwndEdit_);
}

void SearchOverlay::PositionForKeyboard(int keyboardHeight, int gapPixels) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    const int totalHeight = kWindowHeight + gapPixels + keyboardHeight;
    const int x = work.left + (workWidth - kWindowWidth) / 2;
    int y = work.top + (workHeight - totalHeight) / 2;
    if (y < work.top) {
        y = work.top;
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwndEdit_);
}

bool SearchOverlay::Toggle() {
    if (isOpen_) {
        Close();
        return false;
    }

    CreateControls();
    isOpen_ = true;
    stickDirY_ = 0;
    stickRepeatCooldown_ = 0;
    ShowCentered();
    statusMessage_ = L"Search: open";
    SetWindowTextW(hwndStatus_, L"Type a file or folder name. X opens the SofaControl keyboard.");
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void SearchOverlay::Close() {
    KillTimer(hwnd_, kSearchTimerId);
    ShowWindow(hwnd_, SW_HIDE);
    isOpen_ = false;
    stickDirY_ = 0;
    stickRepeatCooldown_ = 0;
    statusMessage_ = L"Search: closed";
    if (ownerWindow_) {
        SetForegroundWindow(ownerWindow_);
    }
}

void SearchOverlay::HideAfterOpen() {
    KillTimer(hwnd_, kSearchTimerId);
    ShowWindow(hwnd_, SW_HIDE);
    isOpen_ = false;
    stickDirY_ = 0;
    stickRepeatCooldown_ = 0;
    statusMessage_ = L"Search: closed";
}

void SearchOverlay::ScheduleSearch() {
    KillTimer(hwnd_, kSearchTimerId);
    SetTimer(hwnd_, kSearchTimerId, kSearchDebounceMs, nullptr);
}

std::wstring SearchOverlay::CurrentQuery() const {
    if (!hwndEdit_) {
        return {};
    }
    const int len = GetWindowTextLengthW(hwndEdit_);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwndEdit_, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

void SearchOverlay::ClearResults(const wchar_t* message) {
    results_.clear();
    SendMessageW(hwndList_, LB_RESETCONTENT, 0, 0);
    SetWindowTextW(hwndStatus_, message);
}

void SearchOverlay::FillResults(const std::vector<std::wstring>& results) {
    results_ = results;
    SendMessageW(hwndList_, LB_RESETCONTENT, 0, 0);
    for (const auto& path : results_) {
        const std::wstring text = DisplayText(path);
        SendMessageW(hwndList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    if (!results_.empty()) {
        SendMessageW(hwndList_, LB_SETCURSEL, 0, 0);
    }

    wchar_t status[128]{};
    swprintf_s(status, L"%zu result%s. A opens the selected item, B closes.",
        results_.size(), results_.size() == 1 ? L"" : L"s");
    SetWindowTextW(hwndStatus_, status);
}

void SearchOverlay::RunSearch() {
    const std::wstring query = CurrentQuery();
    if (query == lastQuery_) {
        return;
    }
    lastQuery_ = query;

    if (QueryTokens(query).empty()) {
        ClearResults(L"Type a file or folder name. X opens the SofaControl keyboard.");
        return;
    }

    SetWindowTextW(hwndStatus_, L"Searching shortcuts, apps, Steam libraries, and user folders...");
    FillResults(FindMatches(query));
}

void SearchOverlay::MoveSelection(int delta) {
    const int count = static_cast<int>(SendMessageW(hwndList_, LB_GETCOUNT, 0, 0));
    if (count <= 0) {
        return;
    }

    int current = static_cast<int>(SendMessageW(hwndList_, LB_GETCURSEL, 0, 0));
    if (current == LB_ERR) {
        current = 0;
    } else {
        current += delta;
        if (current < 0) {
            current = count - 1;
        } else if (current >= count) {
            current = 0;
        }
    }

    SendMessageW(hwndList_, LB_SETCURSEL, current, 0);
}

void SearchOverlay::HandleStickNavigation(float stickY) {
    int dir = 0;
    if (stickY > kStickNavThreshold) {
        dir = 1;
    } else if (stickY < -kStickNavThreshold) {
        dir = -1;
    }

    if (dir == 0) {
        stickDirY_ = 0;
        stickRepeatCooldown_ = 0;
        return;
    }

    const bool directionChanged = dir != stickDirY_;
    if (!directionChanged && stickRepeatCooldown_ > 0) {
        --stickRepeatCooldown_;
        return;
    }

    MoveSelection(dir);
    stickDirY_ = dir;
    stickRepeatCooldown_ = directionChanged ? 1 : kStickRepeatFrames;
}

void SearchOverlay::OpenSelected() {
    const int selected = static_cast<int>(SendMessageW(hwndList_, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected < 0 || selected >= static_cast<int>(results_.size())) {
        return;
    }

    if (beforeOpenHandler_) {
        beforeOpenHandler_();
    }
    ShellExecuteW(hwnd_, L"open", results_[static_cast<size_t>(selected)].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    HideAfterOpen();
}

void SearchOverlay::Update(const SearchOverlayInput& input) {
    if (!isOpen_) {
        return;
    }

    if (input.close) {
        Close();
        return;
    }
    if (input.navUp) {
        MoveSelection(-1);
    } else if (input.navDown) {
        MoveSelection(1);
    } else {
        HandleStickNavigation(input.stickY);
    }
    if (input.open) {
        OpenSelected();
    }
}

LRESULT CALLBACK SearchOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SearchOverlay* self = SearchFromHwnd(hwnd);

    if (msg == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SearchOverlay*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SearchOverlay::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == kEditId && HIWORD(wParam) == EN_CHANGE) {
            ScheduleSearch();
            return 0;
        }
        if (LOWORD(wParam) == kListId && HIWORD(wParam) == LBN_DBLCLK) {
            OpenSelected();
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == kSearchTimerId) {
            KillTimer(hwnd_, kSearchTimerId);
            RunSearch();
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(32, 32, 36));
        SetTextColor(dc, RGB(232, 236, 244));
        static HBRUSH brush = CreateSolidBrush(RGB(32, 32, 36));
        return reinterpret_cast<LRESULT>(brush);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH bg = CreateSolidBrush(RGB(32, 32, 36));
        FillRect(hdc, &client, bg);
        DeleteObject(bg);

        RECT panel{8, 8, client.right - 8, client.bottom - 8};
        DrawPanel(hdc, panel, RGB(32, 32, 36), RGB(90, 90, 98));

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 230, 230));
        HFONT title = MakeSegoeUiFont(-22, FW_SEMIBOLD);
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, title));
        RECT titleRect{kMargin, 20, client.right - kMargin, 48};
        DrawTextW(hdc, L"Search files and folders", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(title);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        Close();
        return 0;

    case WM_DESTROY:
        if (uiFont_) {
            DeleteObject(uiFont_);
            uiFont_ = nullptr;
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
