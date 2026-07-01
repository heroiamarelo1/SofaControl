#pragma once

// Persistent settings stored in %APPDATA%\SofaControl\config.ini.

#include <string>

enum class SelectAction {
    None = 0,
    Sleep = 1,
    Shutdown = 2,
};

enum class ControllerBackend {
    Native = 0,       // Real controller revealed via HidHide in game mode
    Emulated360 = 1,  // Real controller hidden; ViGEm virtual Xbox 360 mirrors it
};

class AppConfig {
public:
    void Load();
    void Save() const;

    // Directory %APPDATA%\SofaControl (created if missing). Empty on failure.
    static std::wstring AppDataDir();

    int SensitivityPercent() const { return sensitivityPercent_; }
    void SetSensitivityPercent(int value) { sensitivityPercent_ = value; }

    SelectAction GetSelectAction() const { return selectAction_; }
    void SetSelectAction(SelectAction value) { selectAction_ = value; }

    bool WakeEnabled() const { return wakeEnabled_; }
    void SetWakeEnabled(bool value) { wakeEnabled_ = value; }

    bool StartupEnabled() const { return startupEnabled_; }
    void SetStartupEnabled(bool value) { startupEnabled_ = value; }

    ControllerBackend GetBackend() const { return backend_; }
    void SetBackend(ControllerBackend value) { backend_ = value; }

    bool PreArmEnabled() const { return preArmEnabled_; }
    void SetPreArmEnabled(bool value) { preArmEnabled_ = value; }

    bool CombosEnabled() const { return combosEnabled_; }
    void SetCombosEnabled(bool value) { combosEnabled_ = value; }

    bool ShowOnNextLaunch() const { return showOnNextLaunch_; }
    void SetShowOnNextLaunch(bool value) { showOnNextLaunch_ = value; }

    // Donation reminder bookkeeping.
    long long FirstRunUnix() const { return firstRunUnix_; }
    void SetFirstRunUnix(long long value) { firstRunUnix_ = value; }

    // 0 = waiting for 14 days, 1 = waiting for 30, 2 = waiting for 45,
    // 3 = finished (no more reminders), 4 = user clicked (thanked, never again).
    int DonateStage() const { return donateStage_; }
    void SetDonateStage(int value) { donateStage_ = value; }

private:
    static std::wstring ConfigPath();

    int sensitivityPercent_ = 100;
    SelectAction selectAction_ = SelectAction::None;
    bool wakeEnabled_ = false;
    bool startupEnabled_ = true;
    ControllerBackend backend_ = ControllerBackend::Native;
    bool preArmEnabled_ = false;
    bool combosEnabled_ = true;
    bool showOnNextLaunch_ = false;
    long long firstRunUnix_ = 0;
    int donateStage_ = 0;
};
