#include "ModeManager.h"

#include <Windows.h>
#include <filesystem>
#include <mmsystem.h>

bool ModeManager::TryToggle() {
    mode_ = (mode_ == AppMode::VirtualMouse) ? AppMode::Passthrough : AppMode::VirtualMouse;
    return true;
}

std::wstring ModeManager::ToggleSoundPath() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    const std::filesystem::path candidates[] = {
        exeDir / L"toggle.wav",
        exeDir / L"assets" / L"toggle.wav",
    };

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path.wstring();
        }
    }

    return exeDir / L"assets" / L"toggle.wav";
}

void ModeManager::PlayToggleSound() const {
    const std::wstring path = ToggleSoundPath();

    if (std::filesystem::exists(path)) {
        PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        return;
    }

    PlaySoundW(L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC);
}
