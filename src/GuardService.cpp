#include "DeviceHider.h"

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kServiceName[] = L"SofaControlGuard";
constexpr wchar_t kServiceDisplayName[] = L"SofaControl HidHide Guard";

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stopEvent = nullptr;
bool g_startupTaskTriggered = false;

std::filesystem::path ModulePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).lexically_normal();
}

std::filesystem::path SofaControlPathBesideService() {
    const auto modulePath = ModulePath();
    if (modulePath.empty()) {
        return {};
    }
    return modulePath.parent_path() / L"SofaControl.exe";
}

void ReportStatus(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHintMs = 0) {
    if (!g_statusHandle) {
        return;
    }

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = win32ExitCode;
    g_status.dwWaitHint = waitHintMs;
    g_status.dwControlsAccepted =
        state == SERVICE_RUNNING ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE) : 0;
    SetServiceStatus(g_statusHandle, &g_status);
}

void ApplyGuardOnce() {
    // The guard now only accelerates SofaControl startup. HidHide is controlled
    // by the interactive app when the user enables pre-arm.
}

bool TriggerSofaControlStartupTask() {
    if (g_startupTaskTriggered) {
        return true;
    }

    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) == 0) {
        return false;
    }

    std::wstring command = L"\"";
    command += systemDir;
    command += L"\\schtasks.exe\" /Run /TN \"SofaControl\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

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

    WaitForSingleObject(process.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    g_startupTaskTriggered = (exitCode == 0);
    return g_startupTaskTriggered;
}

DWORD WINAPI ServiceControlHandlerEx(DWORD control, DWORD eventType, LPVOID /*eventData*/, LPVOID /*context*/) {
    if (control == SERVICE_CONTROL_STOP) {
        ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
        if (g_stopEvent) {
            SetEvent(g_stopEvent);
        }
    }
    if (control == SERVICE_CONTROL_SESSIONCHANGE &&
        (eventType == WTS_SESSION_LOGON || eventType == WTS_SESSION_UNLOCK)) {
        g_startupTaskTriggered = false;
        TriggerSofaControlStartupTask();
    }
    return NO_ERROR;
}

void WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceControlHandlerEx, nullptr);

    if (!g_statusHandle) {
        return;
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        ReportStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 10000);
    ApplyGuardOnce();
    TriggerSofaControlStartupTask();
    ReportStatus(SERVICE_RUNNING);

    DWORD intervalMs = 1000;
    DWORD fastTicksRemaining = 120;
    while (WaitForSingleObject(g_stopEvent, intervalMs) == WAIT_TIMEOUT) {
        ApplyGuardOnce();
        TriggerSofaControlStartupTask();
        if (fastTicksRemaining > 0) {
            --fastTicksRemaining;
            if (fastTicksRemaining == 0) {
                intervalMs = 10000;
            }
        }
    }

    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    ReportStatus(SERVICE_STOPPED);
}

bool RunServiceDispatcher() {
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(kServiceName), ServiceMain},
        {nullptr, nullptr},
    };
    return StartServiceCtrlDispatcherW(table) != FALSE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        const std::wstring arg = argv[1];
        if (arg == L"--apply-once") {
            ApplyGuardOnce();
            return 0;
        }
        if (arg == L"--service-name") {
            MessageBoxW(nullptr, kServiceDisplayName, kServiceName, MB_OK);
            return 0;
        }
    }

    if (RunServiceDispatcher()) {
        return 0;
    }

    if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        ApplyGuardOnce();
        return 0;
    }

    return 1;
}
