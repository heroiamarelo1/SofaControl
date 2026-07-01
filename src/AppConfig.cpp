#include "AppConfig.h"

#include <Windows.h>

#include <filesystem>

namespace {
constexpr wchar_t kSection[] = L"SofaControl";
}

std::wstring AppConfig::AppDataDir() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }

    std::filesystem::path dir = std::filesystem::path(buffer) / L"SofaControl";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.wstring();
}

std::wstring AppConfig::ConfigPath() {
    const std::wstring dir = AppDataDir();
    if (dir.empty()) {
        return {};
    }
    return (std::filesystem::path(dir) / L"config.ini").wstring();
}

void AppConfig::Load() {
    const std::wstring path = ConfigPath();
    if (path.empty()) {
        return;
    }

    sensitivityPercent_ =
        static_cast<int>(GetPrivateProfileIntW(kSection, L"Sensitivity", 100, path.c_str()));

    const int action = static_cast<int>(GetPrivateProfileIntW(kSection, L"SelectAction", 0, path.c_str()));
    selectAction_ = (action == 1) ? SelectAction::Sleep
                  : (action == 2) ? SelectAction::Shutdown
                                  : SelectAction::None;

    wakeEnabled_ = GetPrivateProfileIntW(kSection, L"WakeEnabled", 0, path.c_str()) != 0;
    startupEnabled_ = GetPrivateProfileIntW(kSection, L"StartupEnabled", 1, path.c_str()) != 0;

    const int backend = static_cast<int>(GetPrivateProfileIntW(kSection, L"Backend", 0, path.c_str()));
    backend_ = (backend == 1) ? ControllerBackend::Emulated360
                              : ControllerBackend::Native;
    preArmEnabled_ = GetPrivateProfileIntW(kSection, L"PreArmEnabled", 0, path.c_str()) != 0;
    combosEnabled_ = GetPrivateProfileIntW(kSection, L"CombosEnabled", 1, path.c_str()) != 0;
    showOnNextLaunch_ = GetPrivateProfileIntW(kSection, L"ShowOnNextLaunch", 0, path.c_str()) != 0;

    wchar_t firstRun[32]{};
    GetPrivateProfileStringW(kSection, L"FirstRunUnix", L"0", firstRun, 32, path.c_str());
    firstRunUnix_ = _wtoi64(firstRun);
    donateStage_ = static_cast<int>(GetPrivateProfileIntW(kSection, L"DonateStage", 0, path.c_str()));
}

void AppConfig::Save() const {
    const std::wstring path = ConfigPath();
    if (path.empty()) {
        return;
    }

    auto writeInt = [&](const wchar_t* key, int value) {
        wchar_t buf[16]{};
        _itow_s(value, buf, 10);
        WritePrivateProfileStringW(kSection, key, buf, path.c_str());
    };

    writeInt(L"Sensitivity", sensitivityPercent_);
    writeInt(L"SelectAction", static_cast<int>(selectAction_));
    writeInt(L"WakeEnabled", wakeEnabled_ ? 1 : 0);
    writeInt(L"StartupEnabled", startupEnabled_ ? 1 : 0);
    writeInt(L"Backend", static_cast<int>(backend_));
    writeInt(L"PreArmEnabled", preArmEnabled_ ? 1 : 0);
    writeInt(L"CombosEnabled", combosEnabled_ ? 1 : 0);
    writeInt(L"ShowOnNextLaunch", showOnNextLaunch_ ? 1 : 0);
    writeInt(L"DonateStage", donateStage_);

    wchar_t firstRun[32]{};
    _i64tow_s(firstRunUnix_, firstRun, 32, 10);
    WritePrivateProfileStringW(kSection, L"FirstRunUnix", firstRun, path.c_str());
}
