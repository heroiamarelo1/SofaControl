#include "SearchOverlay.h"

#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <shlobj.h>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"SofaControlSearchOverlay";
constexpr int kEditId = 2101;
constexpr int kListId = 2102;
constexpr int kMaxResults = 80;
constexpr int kMaxCandidates = 500;
constexpr int kMaxVisited = 90000;
constexpr UINT kNoResultRebuildCooldownMs = 10 * 60 * 1000;
constexpr ULONGLONG kDailyRefreshDelayMs = 30ull * 60ull * 1000ull;
constexpr uint32_t kSearchIndexMagic = 0x58494353;  // SCIX
constexpr uint32_t kSearchIndexVersion = 1;
constexpr uint32_t kMaxStoredIndexEntries = 200000;
constexpr uint32_t kMaxStoredPathChars = 32767;

SearchOverlay* SearchFromHwnd(HWND hwnd) {
    return reinterpret_cast<SearchOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

struct SearchCompletion {
    int generation = 0;
    std::wstring query;
    std::vector<SearchResult> results;
};

void DestroyIcons(std::vector<SearchResult>& results) {
    for (auto& result : results) {
        if (result.icon) {
            DestroyIcon(result.icon);
            result.icon = nullptr;
        }
    }
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

bool MatchesQuery(const SearchIndexEntry& entry, const std::vector<std::wstring>& tokens) {
    for (const auto& token : tokens) {
        if (entry.lowerName.find(token) == std::wstring::npos && entry.lowerPath.find(token) == std::wstring::npos) {
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

std::filesystem::path SearchIndexDirectory() {
    PWSTR raw = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        base = raw;
        CoTaskMemFree(raw);
    }
    if (base.empty()) {
        base = EnvPath(L"LOCALAPPDATA");
    }
    if (base.empty()) {
        base = EnvPath(L"APPDATA");
    }
    if (base.empty()) {
        return {};
    }
    return base / L"SofaControl";
}

std::filesystem::path SearchIndexPath() {
    const auto dir = SearchIndexDirectory();
    return dir.empty() ? std::filesystem::path{} : dir / L"search_index.dat";
}

std::filesystem::path SearchIndexTempPath() {
    const auto dir = SearchIndexDirectory();
    return dir.empty() ? std::filesystem::path{} : dir / L"search_index.tmp";
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

bool LooksLikeGamePath(const std::wstring& lowerPath);

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

bool IsProgramOrGameRootPath(const std::wstring& lowerPath) {
    return lowerPath.find(L"\\program files") != std::wstring::npos ||
           lowerPath.find(L"\\steamapps\\common\\") != std::wstring::npos ||
           LooksLikeGamePath(lowerPath);
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

SearchIndexEntry MakeSearchIndexEntry(const std::wstring& path) {
    SearchIndexEntry entry;
    entry.path = path;
    entry.lowerPath = ToLower(path);
    entry.lowerName = ToLower(std::filesystem::path(path).filename().wstring());
    entry.priority = ResultPriority(path);
    return entry;
}

std::wstring TypeLabelForPath(const std::wstring& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return L"Folder";
    }

    const std::wstring ext = ToLower(std::filesystem::path(path).extension().wstring());
    if (ext == L".lnk" || ext == L".url") {
        return L"Shortcut";
    }
    if (ext == L".exe") {
        return L"App";
    }
    return L"File";
}

std::wstring ParentFolderLabel(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return {};
    }

    std::wstring label = parent.filename().wstring();
    if (label.empty()) {
        label = parent.wstring();
    }
    return label;
}

HICON IconForPath(const std::wstring& path) {
    SHFILEINFOW info{};
    const DWORD_PTR result = SHGetFileInfoW(
        path.c_str(),
        0,
        &info,
        sizeof(info),
        SHGFI_ICON | SHGFI_SMALLICON);
    return result ? info.hIcon : nullptr;
}

SearchResult MakeSearchResult(const std::wstring& path) {
    const std::filesystem::path fsPath(path);
    SearchResult result;
    result.path = path;
    result.typeLabel = TypeLabelForPath(path);
    result.folderLabel = ParentFolderLabel(fsPath);
    result.fileLabel = fsPath.filename().wstring();
    return result;
}

bool ShouldIndexPath(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return true;
    }

    const std::wstring lowerPath = ToLower(path.wstring());
    const std::wstring ext = ToLower(path.extension().wstring());
    if (ext == L".exe" || ext == L".lnk" || ext == L".url") {
        return true;
    }

    if (IsProgramOrGameRootPath(lowerPath)) {
        return false;
    }

    return true;
}

std::vector<SearchIndexEntry> BuildSearchIndex(const std::atomic_bool& stopRequested) {
    std::vector<SearchIndexEntry> index;
    index.reserve(32000);

    int visited = 0;
    std::set<std::wstring> seen;
    for (const auto& root : SearchRoots()) {
        if (stopRequested.load() || visited >= kMaxVisited) {
            break;
        }

        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::recursive_directory_iterator end;
        while (!ec && it != end && visited < kMaxVisited && !stopRequested.load()) {
            const auto path = it->path();
            ++visited;

            if (ShouldIndexPath(path)) {
                const std::wstring full = path.wstring();
                const std::wstring lower = ToLower(full);
                if (seen.insert(lower).second) {
                    index.push_back(MakeSearchIndexEntry(full));
                }
            }

            it.increment(ec);
        }
    }

    return index;
}

bool ReadUInt32(std::ifstream& input, uint32_t& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return input.good();
}

bool WriteUInt32(std::ofstream& output, uint32_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return output.good();
}

bool LoadSearchIndexFromDisk(std::vector<SearchIndexEntry>& out) {
    const auto path = SearchIndexPath();
    if (path.empty()) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    if (!ReadUInt32(input, magic) || !ReadUInt32(input, version) || !ReadUInt32(input, count)) {
        return false;
    }
    if (magic != kSearchIndexMagic || version != kSearchIndexVersion || count > kMaxStoredIndexEntries) {
        return false;
    }

    std::vector<SearchIndexEntry> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t length = 0;
        if (!ReadUInt32(input, length) || length == 0 || length > kMaxStoredPathChars) {
            return false;
        }

        std::wstring pathText(length, L'\0');
        input.read(reinterpret_cast<char*>(pathText.data()), static_cast<std::streamsize>(length * sizeof(wchar_t)));
        if (!input.good()) {
            return false;
        }
        loaded.push_back(MakeSearchIndexEntry(pathText));
    }

    out = std::move(loaded);
    return !out.empty();
}

bool SaveSearchIndexToDisk(const std::vector<SearchIndexEntry>& index) {
    const auto finalPath = SearchIndexPath();
    const auto tempPath = SearchIndexTempPath();
    if (finalPath.empty() || tempPath.empty() || index.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(finalPath.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    std::vector<const SearchIndexEntry*> storedEntries;
    storedEntries.reserve(std::min<size_t>(index.size(), kMaxStoredIndexEntries));
    for (const auto& entry : index) {
        if (!entry.path.empty() && entry.path.size() <= kMaxStoredPathChars) {
            storedEntries.push_back(&entry);
            if (storedEntries.size() >= kMaxStoredIndexEntries) {
                break;
            }
        }
    }

    const uint32_t count = static_cast<uint32_t>(storedEntries.size());
    if (!WriteUInt32(output, kSearchIndexMagic) ||
        !WriteUInt32(output, kSearchIndexVersion) ||
        !WriteUInt32(output, count)) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        const auto& path = storedEntries[static_cast<size_t>(i)]->path;
        const uint32_t length = static_cast<uint32_t>(path.size());
        if (!WriteUInt32(output, length)) {
            return false;
        }
        output.write(reinterpret_cast<const char*>(path.data()), static_cast<std::streamsize>(length * sizeof(wchar_t)));
        if (!output.good()) {
            return false;
        }
    }

    output.close();
    if (!output) {
        return false;
    }

    return MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool SearchIndexWasWrittenToday() {
    const auto path = SearchIndexPath();
    if (path.empty()) {
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }

    FILETIME localFileTime{};
    SYSTEMTIME written{};
    SYSTEMTIME now{};
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &localFileTime) ||
        !FileTimeToSystemTime(&localFileTime, &written)) {
        return false;
    }
    GetLocalTime(&now);
    return written.wYear == now.wYear && written.wMonth == now.wMonth && written.wDay == now.wDay;
}

std::vector<SearchResult> FindMatches(const std::wstring& query, const std::vector<SearchIndexEntry>& index) {
    std::vector<SearchIndexEntry> candidates;
    const auto tokens = QueryTokens(query);
    if (tokens.empty()) {
        return {};
    }

    candidates.reserve(kMaxCandidates);
    for (const auto& entry : index) {
        if (MatchesQuery(entry, tokens)) {
            candidates.push_back(entry);
            if (static_cast<int>(candidates.size()) >= kMaxCandidates) {
                break;
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const SearchIndexEntry& a, const SearchIndexEntry& b) {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }

        const int nameCmp = _wcsicmp(a.lowerName.c_str(), b.lowerName.c_str());
        if (nameCmp != 0) {
            return nameCmp < 0;
        }
        return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
    });

    if (static_cast<int>(candidates.size()) > kMaxResults) {
        candidates.resize(kMaxResults);
    }

    std::vector<SearchResult> results;
    results.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        results.push_back(MakeSearchResult(candidate.path));
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

    if (!hwnd_) {
        return false;
    }

    appStartMs_ = GetTickCount64();

    std::vector<SearchIndexEntry> loadedIndex;
    if (LoadSearchIndexFromDisk(loadedIndex)) {
        std::lock_guard<std::mutex> lock(searchIndexMutex_);
        searchIndex_ = std::make_shared<const std::vector<SearchIndexEntry>>(std::move(loadedIndex));
        searchIndexReady_ = true;
    }

    SetTimer(hwnd_, kDailyIndexTimerId, kDailyIndexPollMs, nullptr);
    return true;
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
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
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
    SendMessageW(hwndList_, LB_SETITEMHEIGHT, 0, kResultRowHeight);
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
    EnsureSearchIndexStarted();
    isOpen_ = true;
    stickDirY_ = 0;
    stickRepeatCooldown_ = 0;
    ShowCentered();
    statusMessage_ = L"Search: open";
    SetWindowTextW(hwndStatus_, searchIndexReady_.load() ? L"Type a file or folder name. X opens the SofaControl keyboard."
                                                         : L"Indexing apps, shortcuts, games, and files...");
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void SearchOverlay::Close() {
    KillTimer(hwnd_, kSearchTimerId);
    ++searchGeneration_;
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
    ++searchGeneration_;
    ShowWindow(hwnd_, SW_HIDE);
    isOpen_ = false;
    stickDirY_ = 0;
    stickRepeatCooldown_ = 0;
    statusMessage_ = L"Search: closed";
}

void SearchOverlay::EnsureSearchIndexStarted() {
    BeginIndexRebuild(false);
}

void SearchOverlay::BeginIndexRebuild(bool force) {
    if (searchIndexRunning_.load()) {
        return;
    }
    if (!force && searchIndexReady_.load()) {
        return;
    }

    if (searchIndexThread_.joinable()) {
        searchIndexThread_.join();
    }

    stopSearchIndex_ = false;
    searchIndexRunning_ = true;
    searchIndexThread_ = std::thread([this]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        auto index = BuildSearchIndex(stopSearchIndex_);
        if (!stopSearchIndex_.load()) {
            SaveSearchIndexToDisk(index);
            {
                std::lock_guard<std::mutex> lock(searchIndexMutex_);
                searchIndex_ = std::make_shared<const std::vector<SearchIndexEntry>>(std::move(index));
            }
            searchIndexReady_ = true;
            if (hwnd_) {
                PostMessageW(hwnd_, kSearchIndexReadyMsg, 0, 0);
            }
        }
        searchIndexRunning_ = false;
    });
}

void SearchOverlay::CheckScheduledIndexRefresh() {
    if (dailyIndexRefreshStarted_) {
        return;
    }

    if (GetTickCount64() - appStartMs_ < kDailyRefreshDelayMs) {
        return;
    }

    dailyIndexRefreshStarted_ = true;
    if (!SearchIndexWasWrittenToday()) {
        BeginIndexRebuild(true);
    }
}

void SearchOverlay::StopSearchIndex() {
    stopSearchIndex_ = true;
    if (searchIndexThread_.joinable()) {
        searchIndexThread_.join();
    }
    searchIndexRunning_ = false;
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
    DestroyResultIcons();
    results_.clear();
    SendMessageW(hwndList_, LB_RESETCONTENT, 0, 0);
    SetWindowTextW(hwndStatus_, message);
}

void SearchOverlay::FillResults(const std::vector<SearchResult>& results) {
    DestroyResultIcons();
    results_ = results;
    SendMessageW(hwndList_, LB_RESETCONTENT, 0, 0);
    for (const auto& result : results_) {
        SendMessageW(hwndList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(result.fileLabel.c_str()));
    }

    if (!results_.empty()) {
        SendMessageW(hwndList_, LB_SETCURSEL, 0, 0);
    }
    EnsureVisibleResultIcons();

    wchar_t status[128]{};
    swprintf_s(status, L"%zu result%s. START opens the selected item, Y closes.",
        results_.size(), results_.size() == 1 ? L"" : L"s");
    SetWindowTextW(hwndStatus_, status);
}

void SearchOverlay::RunSearch() {
    const std::wstring query = CurrentQuery();
    if (query == lastQuery_) {
        return;
    }

    if (QueryTokens(query).empty()) {
        ++searchGeneration_;
        lastQuery_ = query;
        ClearResults(L"Type a file or folder name. X opens the SofaControl keyboard.");
        return;
    }

    if (!searchIndexReady_.load()) {
        SetWindowTextW(hwndStatus_, searchIndexRunning_.load() ? L"Indexing apps, shortcuts, games, and files..."
                                                               : L"Preparing search index...");
        return;
    }

    lastQuery_ = query;
    BeginAsyncSearch(query);
}

void SearchOverlay::BeginAsyncSearch(const std::wstring& query) {
    std::shared_ptr<const std::vector<SearchIndexEntry>> indexSnapshot;
    {
        std::lock_guard<std::mutex> lock(searchIndexMutex_);
        indexSnapshot = searchIndex_;
    }

    if (!indexSnapshot) {
        SetWindowTextW(hwndStatus_, L"Preparing search index...");
        return;
    }

    const int generation = ++searchGeneration_;
    const HWND targetHwnd = hwnd_;
    const UINT resultsMessage = kSearchResultsReadyMsg;
    SetWindowTextW(hwndStatus_, L"Searching index...");

    std::thread([targetHwnd, generation, query, indexSnapshot, resultsMessage]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        auto* completion = new SearchCompletion;
        completion->generation = generation;
        completion->query = query;
        completion->results = FindMatches(query, *indexSnapshot);

        if (!IsWindow(targetHwnd) ||
            !PostMessageW(targetHwnd, resultsMessage, 0, reinterpret_cast<LPARAM>(completion))) {
            delete completion;
        }
    }).detach();
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
    EnsureVisibleResultIcons();
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
    ShellExecuteW(hwnd_, L"open", results_[static_cast<size_t>(selected)].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    HideAfterOpen();
}

void SearchOverlay::DestroyResultIcons() {
    DestroyIcons(results_);
}

void SearchOverlay::EnsureVisibleResultIcons() {
    if (!hwndList_ || results_.empty()) {
        return;
    }

    int top = static_cast<int>(SendMessageW(hwndList_, LB_GETTOPINDEX, 0, 0));
    if (top == LB_ERR || top < 0) {
        top = 0;
    }

    const int first = top > 0 ? top : 0;
    const int resultCount = static_cast<int>(results_.size());
    const int last = (first + 10 < resultCount) ? first + 10 : resultCount;

    for (int i = 0; i < resultCount; ++i) {
        auto& result = results_[static_cast<size_t>(i)];
        if (i >= first && i < last) {
            if (!result.icon) {
                result.icon = IconForPath(result.path);
            }
        } else if (result.icon) {
            DestroyIcon(result.icon);
            result.icon = nullptr;
        }
    }

    RECT listRect{};
    GetClientRect(hwndList_, &listRect);
    InvalidateRect(hwndList_, &listRect, FALSE);
}

void SearchOverlay::DrawResultItem(const DRAWITEMSTRUCT* dis) const {
    if (!dis || dis->CtlID != kListId || dis->itemID == static_cast<UINT>(-1) ||
        dis->itemID >= results_.size()) {
        return;
    }

    const SearchResult& result = results_[dis->itemID];
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const COLORREF bg = selected ? RGB(55, 76, 108) : RGB(32, 32, 36);
    const COLORREF typeColor = RGB(116, 180, 255);
    const COLORREF folderColor = RGB(168, 176, 190);
    const COLORREF fileColor = RGB(238, 240, 246);

    HBRUSH bgBrush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(dis->hDC, TRANSPARENT);
    HFONT oldFont = uiFont_ ? static_cast<HFONT>(SelectObject(dis->hDC, uiFont_)) : nullptr;

    RECT rc = dis->rcItem;
    rc.left += 8;
    rc.right -= 8;

    RECT typeRect{rc.left, rc.top + 4, rc.left + 78, rc.bottom - 4};
    std::wstring typeText = L"[" + result.typeLabel + L"]";
    SetTextColor(dis->hDC, typeColor);
    DrawTextW(dis->hDC, typeText.c_str(), -1, &typeRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    const int iconX = typeRect.right + 8;
    const int iconY = rc.top + ((rc.bottom - rc.top) - 16) / 2;
    if (result.icon) {
        DrawIconEx(dis->hDC, iconX, iconY, result.icon, 16, 16, 0, nullptr, DI_NORMAL);
    }

    RECT folderRect{iconX + 28, rc.top + 4, iconX + 210, rc.bottom - 4};
    SetTextColor(dis->hDC, folderColor);
    DrawTextW(
        dis->hDC,
        result.folderLabel.empty() ? L"(root)" : result.folderLabel.c_str(),
        -1,
        &folderRect,
        DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    RECT fileRect{folderRect.right + 14, rc.top + 4, rc.right, rc.bottom - 4};
    SetTextColor(dis->hDC, fileColor);
    DrawTextW(dis->hDC, result.fileLabel.c_str(), -1, &fileRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    if (oldFont) {
        SelectObject(dis->hDC, oldFont);
    }

    if (dis->itemState & ODS_FOCUS) {
        DrawFocusRect(dis->hDC, &dis->rcItem);
    }
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
    if (msg == kSearchIndexReadyMsg) {
        lastQuery_.clear();
        RunSearch();
        return 0;
    }

    if (msg == kSearchResultsReadyMsg) {
        auto* completion = reinterpret_cast<SearchCompletion*>(lParam);
        if (!completion) {
            return 0;
        }

        if (completion->generation == searchGeneration_.load() && completion->query == lastQuery_) {
            FillResults(completion->results);
            const auto tokens = QueryTokens(completion->query);
            const ULONGLONG now = GetTickCount64();
            if (completion->results.empty() &&
                !searchIndexRunning_.load() &&
                !tokens.empty() &&
                completion->query.size() >= 3 &&
                now - lastNoResultRebuildMs_ >= kNoResultRebuildCooldownMs) {
                lastNoResultRebuildMs_ = now;
                SetWindowTextW(hwndStatus_, L"No results. Refreshing the search index...");
                BeginIndexRebuild(true);
            }
        }
        delete completion;
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == kEditId && HIWORD(wParam) == EN_CHANGE) {
            ScheduleSearch();
            return 0;
        }
        if (LOWORD(wParam) == kListId && HIWORD(wParam) == LBN_SELCHANGE) {
            EnsureVisibleResultIcons();
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
        if (wParam == kDailyIndexTimerId) {
            CheckScheduledIndexRefresh();
            return 0;
        }
        break;

    case WM_DRAWITEM:
        DrawResultItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;

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
        ++searchGeneration_;
        KillTimer(hwnd_, kDailyIndexTimerId);
        StopSearchIndex();
        DestroyResultIcons();
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
