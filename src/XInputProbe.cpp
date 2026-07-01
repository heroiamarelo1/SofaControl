#include <Windows.h>
#include <Xinput.h>

#include <string>

#pragma comment(lib, "xinput.lib")

namespace {

std::wstring StateText(DWORD slot, DWORD result) {
    std::wstring text = L"XInput slot ";
    text += std::to_wstring(slot);
    text += L": ";
    text += (result == ERROR_SUCCESS) ? L"connected" : L"not connected";
    text += L" (";
    text += std::to_wstring(result);
    text += L")\r\n";
    return text;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        CloseHandle(file);
        return false;
    }

    std::string utf8(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), needed, nullptr, nullptr);

    DWORD written = 0;
    const BOOL ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring outputPath;
    if (argc > 1) {
        outputPath = argv[1];
    }

    bool anyConnected = false;
    std::wstring report;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        XINPUT_STATE state{};
        const DWORD result = XInputGetState(slot, &state);
        if (result == ERROR_SUCCESS) {
            anyConnected = true;
        }
        report += StateText(slot, result);
    }

    if (!outputPath.empty()) {
        WriteUtf8File(outputPath, report);
    }

    return anyConnected ? 2 : 0;
}
