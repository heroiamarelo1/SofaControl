#pragma once

// System-level actions triggered by controller combos / UI toggles.

#include <Windows.h>

#include <string>

namespace SystemActions {

// Gracefully close the current foreground application (WM_CLOSE). Skips our own
// windows and the desktop shell. Returns true if a close request was posted.
bool CloseForegroundApplication(HWND ownWindow, HWND keyboardWindow);

// Put the machine to sleep.
bool Sleep();

// Shut the machine down (acquires SE_SHUTDOWN privilege).
bool Shutdown();

// Run-at-Windows-startup via Task Scheduler, with highest privileges so
// SofaControl can control elevated installer windows after login.
bool IsRunAtStartup();
bool SetRunAtStartup(bool enable);

// Arm/disarm Xbox controller devices to wake the PC from sleep.
// Uses an elevated powercfg helper (UAC prompt). Armed device names are stored
// under appDataDir so they can be disarmed later. Returns true on success.
bool SetControllerWake(bool enable, const std::wstring& appDataDir, std::wstring& statusOut);
bool SetWakeSignInBypass(bool enable, const std::wstring& appDataDir, std::wstring& statusOut);

}  // namespace SystemActions
