#include "SystemActions.h"

#include <powrprof.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <string>

#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"SofaControl";
constexpr wchar_t kStartupTaskName[] = L"SofaControl";

std::wstring CurrentExePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring TempScriptPath(const wchar_t* fileName) {
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) {
        return {};
    }
    return (std::filesystem::path(tempPath) / fileName).wstring();
}

std::wstring PowerShellSingleQuoted(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'\'');
    for (wchar_t ch : value) {
        escaped.push_back(ch);
        if (ch == L'\'') {
            escaped.push_back(L'\'');
        }
    }
    escaped.push_back(L'\'');
    return escaped;
}

bool RunHiddenWait(const std::wstring& file, const std::wstring& parameters, DWORD& exitCodeOut) {
    std::wstring commandLine = L"\"" + file + L"\"";
    if (!parameters.empty()) {
        commandLine += L" " + parameters;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        return false;
    }

    WaitForSingleObject(process.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    exitCodeOut = code;
    return true;
}

bool IsLegacyRunValuePresent() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LONG status = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

void RemoveLegacyRunValue() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
}

bool IsShellWindowClass(HWND hwnd) {
    wchar_t className[64]{};
    GetClassNameW(hwnd, className, 64);
    return wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0 ||
           wcscmp(className, L"Shell_TrayWnd") == 0;
}

INPUT MakeKeyInput(WORD virtualKey, DWORD flags) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = flags;
    return input;
}

bool TapAltF4() {
    INPUT inputs[4]{
        MakeKeyInput(VK_MENU, 0),
        MakeKeyInput(VK_F4, 0),
        MakeKeyInput(VK_F4, KEYEVENTF_KEYUP),
        MakeKeyInput(VK_MENU, KEYEVENTF_KEYUP),
    };
    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

void EnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return;
    }

    LUID luid{};
    if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &luid)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(token);
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return false;
    }
    std::string utf8(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), needed, nullptr, nullptr);

    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return out.good();
}

// Run a PowerShell script file elevated and wait for it to finish.
bool RunElevatedScript(const std::wstring& scriptPath, DWORD& exitCodeOut) {
    std::wstring params =
        L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptPath + L"\"";

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = L"powershell.exe";
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info) || !info.hProcess) {
        return false;
    }

    WaitForSingleObject(info.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    exitCodeOut = code;
    return true;
}

}  // namespace

namespace SystemActions {

bool CloseForegroundApplication(HWND ownWindow, HWND keyboardWindow) {
    HWND fg = GetForegroundWindow();
    if (!fg) {
        return false;
    }
    if (fg == ownWindow || fg == keyboardWindow) {
        return false;
    }
    if (IsShellWindowClass(fg)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(fg, &processId);
    if (processId == GetCurrentProcessId()) {
        return false;
    }

    SetForegroundWindow(fg);
    if (TapAltF4()) {
        return true;
    }

    return PostMessageW(fg, WM_CLOSE, 0, 0) != FALSE;
}

bool Sleep() {
    // bHibernate = FALSE -> sleep; bForce = FALSE; bWakeupEventsDisabled = FALSE.
    return SetSuspendState(FALSE, FALSE, FALSE) != FALSE;
}

bool Shutdown() {
    EnableShutdownPrivilege();
    return ExitWindowsEx(
               EWX_SHUTDOWN | EWX_POWEROFF,
               SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED) != 0;
}

bool IsRunAtStartup() {
    DWORD exitCode = 1;
    if (RunHiddenWait(L"schtasks.exe", L"/Query /TN \"" + std::wstring(kStartupTaskName) + L"\"", exitCode) &&
        exitCode == 0) {
        return true;
    }
    return IsLegacyRunValuePresent();
}

bool SetRunAtStartup(bool enable) {
    const std::wstring scriptPath = TempScriptPath(enable ? L"sofacontrol_startup_enable.ps1"
                                                          : L"sofacontrol_startup_disable.ps1");
    if (scriptPath.empty()) {
        return false;
    }

    std::wstring script =
        L"$ErrorActionPreference='Stop'\r\n"
        L"$taskName='SofaControl'\r\n";

    if (enable) {
        script +=
            L"$exe=" + PowerShellSingleQuoted(CurrentExePath()) + L"\r\n"
            L"$action=New-ScheduledTaskAction -Execute $exe\r\n"
            L"$trigger=New-ScheduledTaskTrigger -AtLogOn\r\n"
            L"$user=[System.Security.Principal.WindowsIdentity]::GetCurrent().Name\r\n"
            L"$principal=New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest\r\n"
            L"Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Description 'Start SofaControl elevated at logon.' -Force | Out-Null\r\n";
    } else {
        script +=
            L"Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue\r\n";
    }

    script +=
        L"Remove-ItemProperty -Path 'HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Run' -Name 'SofaControl' -ErrorAction SilentlyContinue\r\n";

    if (!WriteUtf8File(scriptPath, script)) {
        return false;
    }

    DWORD exitCode = 1;
    const bool launched = RunElevatedScript(scriptPath, exitCode);
    RemoveLegacyRunValue();
    return launched && exitCode == 0;
}

bool SetControllerWake(bool enable, const std::wstring& appDataDir, std::wstring& statusOut) {
    if (appDataDir.empty()) {
        statusOut = L"Wake: could not resolve config folder.";
        return false;
    }

    const std::wstring armedList = (std::filesystem::path(appDataDir) / L"wake_armed.txt").wstring();
    const std::wstring scriptPath =
        (std::filesystem::path(appDataDir) / (enable ? L"wake_enable.ps1" : L"wake_disable.ps1")).wstring();

    std::wstring script;
    if (enable) {
        // Match Xbox controller / wireless adapter entries among wake-capable devices.
        script =
            L"$ErrorActionPreference='SilentlyContinue'\r\n"
            L"$armed='" + armedList + L"'\r\n"
            L"$programmable = powercfg /devicequery wake_programmable\r\n"
            L"$any = powercfg /devicequery wake_from_any\r\n"
            L"$devs = @($programmable + $any) | Where-Object { $_ } | Sort-Object -Unique\r\n"
            L"$match = $devs | Where-Object { $_ -match 'Xbox' -or $_ -match 'XINPUT' -or $_ -match 'Controller' -or $_ -match 'Wireless' }\r\n"
            L"foreach ($d in $match) { powercfg /deviceenablewake \"$d\" }\r\n"
            L"powercfg /setacvalueindex SCHEME_CURRENT SUB_USB USBSELECTIVE 0 | Out-Null\r\n"
            L"powercfg /setdcvalueindex SCHEME_CURRENT SUB_USB USBSELECTIVE 0 | Out-Null\r\n"
            L"powercfg /setactive SCHEME_CURRENT | Out-Null\r\n"
            L"$match | Set-Content -Encoding Unicode -Path $armed\r\n";
    } else {
        script =
            L"$ErrorActionPreference='SilentlyContinue'\r\n"
            L"$armed='" + armedList + L"'\r\n"
            L"if (Test-Path $armed) {\r\n"
            L"  Get-Content $armed | ForEach-Object { if ($_ -and $_.Trim()) { powercfg /devicedisablewake \"$_\" } }\r\n"
            L"  Remove-Item $armed -Force\r\n"
            L"}\r\n";
    }

    if (!WriteUtf8File(scriptPath, script)) {
        statusOut = L"Wake: failed to write helper script.";
        return false;
    }

    DWORD exitCode = 0;
    if (!RunElevatedScript(scriptPath, exitCode)) {
        statusOut = L"Wake: elevation cancelled or failed.";
        return false;
    }

    statusOut = enable ? L"Wake: enabled for supported sleep states (firmware/device support required)."
                       : L"Wake: controller wake disabled.";
    return true;
}

}  // namespace SystemActions
