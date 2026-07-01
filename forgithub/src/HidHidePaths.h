#pragma once

// Path conversion compatible with HidHide whitelist format (ported from HidHide Volume.cpp, MIT).

#include <filesystem>
#include <string>
#include <vector>

// C:\path\app.exe -> \Device\HarddiskVolumeN\path\app.exe
std::wstring FileNameToFullImageName(const std::filesystem::path& fullyQualifiedFileName);

// \Device\HarddiskVolumeN\path\app.exe -> C:\path\app.exe (best effort)
std::filesystem::path FullImageNameToFileName(const std::wstring& fullImageName);

// Current process exe path (normalized).
std::filesystem::path CurrentExecutablePath();

// True if whitelist already contains this executable (DOS or drive-letter path).
bool IsExecutableWhitelisted(
    const std::vector<std::wstring>& whitelist,
    const std::filesystem::path& executablePath);
