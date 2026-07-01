#pragma once

// App settings — virtual mouse sensitivity (never changes the physical mouse).

class AppSettings {
public:
    AppSettings();

    // Slider multiplier (1.0 = base speed matching Windows mouse settings).
    float SensitivityMultiplier() const { return multiplier_; }
    void SetSensitivityMultiplier(float value);

    // Base factor read from Windows (SPI_GETMOUSESPEED, 1–20 → ~0.1–2.0).
    float WindowsMouseBaseFactor() const { return windowsBase_; }

    static constexpr int kDefaultSliderPercent = 100;
    static constexpr int kMinSliderPercent = 25;
    static constexpr int kMaxSliderPercent = 400;

private:
    float windowsBase_ = 1.0f;
    float multiplier_ = 1.0f;

    void ReadWindowsMouseSpeed();
};
