#include "AppSettings.h"

#include <Windows.h>

AppSettings::AppSettings() {
    ReadWindowsMouseSpeed();
    multiplier_ = 1.0f;
}

void AppSettings::ReadWindowsMouseSpeed() {
    // Read physical mouse speed (1–20). We never change it — only use it as a reference.
    int speed = 10;
    SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
    windowsBase_ = static_cast<float>(speed) / 10.0f;
}

void AppSettings::SetSensitivityMultiplier(float value) {
    multiplier_ = value;
}
