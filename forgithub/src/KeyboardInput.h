#pragma once

// Send synthetic keyboard/mouse wheel events via SendInput.

#include <Windows.h>

class KeyboardInput {
public:
    void TapVirtualKey(WORD virtualKey, bool shiftHeld = false);
    void TypeCharacter(wchar_t character, bool shiftHeld);
    void TapCombo(WORD modifierKey, WORD virtualKey);
    void ModifierDown(WORD virtualKey);
    void ModifierUp(WORD virtualKey);
    void TapKey(WORD virtualKey);
    void CtrlWheelScroll(int directionSteps);
    void WheelScroll(int directionSteps);
    void ReleaseAllModifiers();
    void ShowDesktop();

    void Backspace();
    void ArrowLeft();
    void ArrowRight();
    void DiscreteKeyDown(WORD virtualKey);
    void DiscreteKeyUp(WORD virtualKey);
    void Enter();
    void Space();
    void Escape();

private:
    bool altHeld_ = false;
    bool ctrlHeld_ = false;
    bool shiftHeld_ = false;

    void TapVirtualKeyPair(WORD virtualKey);
    void SendDiscreteKey(WORD virtualKey, DWORD flags);
    void SendKeyInputs(const INPUT* inputs, UINT count);
    void SetModifierHeld(WORD virtualKey, bool held);
};
