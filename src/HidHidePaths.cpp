#include "HidHidePaths.h"

#include <Windows.h>

#include <cwctype>
#include <functional>
#include <set>

namespace {

using MountCallback = std::function<bool(const std::wstring& volumeName, const std::filesystem::path& mountPoint)>;

std::wstring ToLower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value;
}

bool EndsWithCaseInsensitive(const std::wstring& haystack, const std::wstring& suffix) {
    if (suffix.size() > haystack.size()) {
        return false;
    }
    return _wcsicmp(haystack.c_str() + haystack.size() - suffix.size(), suffix.c_str()) == 0;
}

std::set<std::filesystem::path> VolumeMountPoints(const std::wstring& volumeName) {
    std::set<std::filesystem::path> result;

    DWORD needed = 0;
    if (!GetVolumePathNamesForVolumeNameW(volumeName.c_str(), nullptr, 0, &needed)) {
        if (GetLastError() != ERROR_MORE_DATA) {
            return result;
        }
    }

    std::vector<wchar_t> buffer(needed);
    if (!GetVolumePathNamesForVolumeNameW(volumeName.c_str(), buffer.data(), needed, &needed)) {
        return result;
    }

    for (const wchar_t* cursor = buffer.data(); *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
        result.emplace(cursor);
    }
    return result;
}

std::wstring DosDeviceNameForVolumeName(const std::wstring& volumeName) {
    if (volumeName.size() < 6) {
        return {};
    }

    const std::wstring stripped = volumeName.substr(4, volumeName.size() - 5);
    std::vector<wchar_t> buffer(512);
    if (QueryDosDeviceW(stripped.c_str(), buffer.data(), static_cast<DWORD>(buffer.size())) == 0) {
        return {};
    }
    return buffer.data();
}

void IterateAllVolumeMountPoints(const MountCallback& callback) {
    wchar_t volumeName[MAX_PATH]{};
    const HANDLE findHandle = FindFirstVolumeW(volumeName, static_cast<DWORD>(std::size(volumeName)));
    if (findHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    auto closeFind = [&findHandle]() { FindVolumeClose(findHandle); };

    while (true) {
        for (const auto& mountPoint : VolumeMountPoints(volumeName)) {
            if (!callback(volumeName, mountPoint)) {
                closeFind();
                return;
            }
        }

        if (!FindNextVolumeW(findHandle, volumeName, static_cast<DWORD>(std::size(volumeName)))) {
            if (GetLastError() != ERROR_NO_MORE_FILES) {
                closeFind();
                return;
            }
            break;
        }
    }

    closeFind();
}

std::filesystem::path FindVolumeMountPointForFullyQualifiedFileName(const std::filesystem::path& fullyQualifiedFileName) {
    const std::wstring native = fullyQualifiedFileName.wstring();
    std::wstring bestMatch;

    IterateAllVolumeMountPoints([&](const std::wstring& /*volumeName*/, const std::filesystem::path& mountPoint) {
        const std::wstring mountNative = mountPoint.wstring();
        if (native.size() >= mountNative.size() &&
            _wcsnicmp(native.c_str(), mountNative.c_str(), mountNative.size()) == 0 &&
            mountNative.size() > bestMatch.size()) {
            bestMatch = mountNative;
        }
        return true;
    });

    return std::filesystem::path(bestMatch);
}

std::wstring VolumeNameForVolumeMountPoint(const std::filesystem::path& volumeMountPoint) {
    wchar_t volumeName[MAX_PATH]{};
    if (!GetVolumeNameForVolumeMountPointW(volumeMountPoint.c_str(), volumeName, MAX_PATH)) {
        return {};
    }
    return volumeName;
}

}  // namespace

std::filesystem::path CurrentExecutablePath() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::filesystem::path(exePath).lexically_normal();
}

std::wstring FileNameToFullImageName(const std::filesystem::path& fullyQualifiedFileName) {
    if (!fullyQualifiedFileName.is_absolute()) {
        return {};
    }

    const std::filesystem::path mountPoint = FindVolumeMountPointForFullyQualifiedFileName(fullyQualifiedFileName);
    if (mountPoint.empty()) {
        return {};
    }

    const std::wstring volumeName = VolumeNameForVolumeMountPoint(mountPoint);
    if (volumeName.empty()) {
        return {};
    }

    const std::wstring dosDeviceName = DosDeviceNameForVolumeName(volumeName);
    if (dosDeviceName.empty()) {
        return {};
    }

    std::wstring tail = fullyQualifiedFileName.wstring().substr(mountPoint.wstring().size());
    if (!tail.empty() && tail.front() == L'\\') {
        tail.erase(tail.begin());
    }

    return dosDeviceName + L"\\" + tail;
}

std::filesystem::path FullImageNameToFileName(const std::wstring& fullImageName) {
    std::filesystem::path result;

    IterateAllVolumeMountPoints([&](const std::wstring& volumeName, const std::filesystem::path& mountPoint) {
        const std::wstring dosDeviceName = DosDeviceNameForVolumeName(volumeName);
        if (dosDeviceName.empty()) {
            return true;
        }

        if (fullImageName.rfind(dosDeviceName, 0) != 0) {
            return true;
        }

        std::wstring tail = fullImageName.substr(dosDeviceName.size());
        if (!tail.empty() && tail.front() == L'\\') {
            tail.erase(tail.begin());
        }

        result = mountPoint / tail;
        return false;
    });

    return result.lexically_normal();
}

bool IsExecutableWhitelisted(
    const std::vector<std::wstring>& whitelist,
    const std::filesystem::path& executablePath) {
    if (whitelist.empty()) {
        return false;
    }

    const std::filesystem::path normalizedExe = executablePath.lexically_normal();
    const std::wstring exePath = normalizedExe.wstring();
    const std::wstring exeLower = ToLower(exePath);
    const std::wstring exeFile = normalizedExe.filename().wstring();
    const std::wstring exeFileLower = ToLower(exeFile);

    const std::wstring fullImageName = FileNameToFullImageName(normalizedExe);
    const std::wstring fullImageLower = ToLower(fullImageName);

    for (const auto& entry : whitelist) {
        if (entry.empty()) {
            continue;
        }

        if (_wcsicmp(entry.c_str(), exePath.c_str()) == 0) {
            return true;
        }

        if (!fullImageName.empty() && _wcsicmp(entry.c_str(), fullImageName.c_str()) == 0) {
            return true;
        }

        if (!fullImageLower.empty() && ToLower(entry) == fullImageLower) {
            return true;
        }

        const std::filesystem::path resolved = FullImageNameToFileName(entry);
        if (!resolved.empty()) {
            if (_wcsicmp(resolved.wstring().c_str(), exePath.c_str()) == 0) {
                return true;
            }
            if (ToLower(resolved.wstring()) == exeLower) {
                return true;
            }
        }

        if (EndsWithCaseInsensitive(entry, exeFile) || EndsWithCaseInsensitive(entry, exeFileLower)) {
            const std::wstring entryLower = ToLower(entry);
            if (entryLower.find(exeFileLower) != std::wstring::npos) {
                if (resolved.empty() || _wcsicmp(resolved.wstring().c_str(), exePath.c_str()) == 0) {
                    return true;
                }
            }
        }
    }

    return false;
}
