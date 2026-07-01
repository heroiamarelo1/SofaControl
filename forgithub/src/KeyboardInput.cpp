#include "KeyboardInput.h"

namespace {

INPUT MakeKeyInput(WORD virtualKey, DWORD flags) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = flags;
    return input;
}

INPUT MakeWheelInput(int steps) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(steps * WHEEL_DELTA);
    return input;
}

}  // namespace

void KeyboardInput::SendKeyInputs(const INPUT* inputs, UINT count) {
    SendInput(count, const_cast<INPUT*>(inputs), sizeof(INPUT));
}

void KeyboardInput::TapVirtualKeyPair(WORD virtualKey) {
    DWORD keyDownFlags = 0;
    DWORD keyUpFlags = KEYEVENTF_KEYUP;
    if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == VK_UP || virtualKey == VK_DOWN) {
        keyDownFlags = KEYEVENTF_EXTENDEDKEY;
        keyUpFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    }

    INPUT inputs[2]{
        MakeKeyInput(virtualKey, keyDownFlags),
        MakeKeyInput(virtualKey, keyUpFlags),
    };
    SendKeyInputs(inputs, 2);
}

void KeyboardInput::SetModifierHeld(WORD virtualKey, bool held) {
    bool* tracked = nullptr;
    if (virtualKey == VK_MENU) {
        tracked = &altHeld_;
    } else if (virtualKey == VK_CONTROL) {
        tracked = &ctrlHeld_;
    } else if (virtualKey == VK_SHIFT) {
        tracked = &shiftHeld_;
    } else {
        return;
    }

    if (*tracked == held) {
        return;
    }

    SendKeyInputs(&MakeKeyInput(virtualKey, held ? 0 : KEYEVENTF_KEYUP), 1);
    *tracked = held;
}

void KeyboardInput::ModifierDown(WORD virtualKey) {
    SetModifierHeld(virtualKey, true);
}

void KeyboardInput::ModifierUp(WORD virtualKey) {
    SetModifierHeld(virtualKey, false);
}

void KeyboardInput::ReleaseAllModifiers() {
    if (altHeld_) {
        ModifierUp(VK_MENU);
    }
    if (ctrlHeld_) {
        ModifierUp(VK_CONTROL);
    }
    if (shiftHeld_) {
        ModifierUp(VK_SHIFT);
    }
}

void KeyboardInput::TapKey(WORD virtualKey) {
    TapVirtualKeyPair(virtualKey);
}

void KeyboardInput::TapCombo(WORD modifierKey, WORD virtualKey) {
    ModifierDown(modifierKey);
    TapVirtualKeyPair(virtualKey);
    ModifierUp(modifierKey);
}

void KeyboardInput::ShowDesktop() {
    INPUT inputs[4]{
        MakeKeyInput(VK_LWIN, 0),
        MakeKeyInput('D', 0),
        MakeKeyInput('D', KEYEVENTF_KEYUP),
        MakeKeyInput(VK_LWIN, KEYEVENTF_KEYUP),
    };
    SendKeyInputs(inputs, 4);
}

void KeyboardInput::CtrlWheelScroll(int directionSteps) {
    if (directionSteps == 0) {
        return;
    }

    ModifierDown(VK_CONTROL);
    SendKeyInputs(&MakeWheelInput(directionSteps), 1);
    ModifierUp(VK_CONTROL);
}

void KeyboardInput::WheelScroll(int directionSteps) {
    if (directionSteps == 0) {
        return;
    }

    SendKeyInputs(&MakeWheelInput(directionSteps), 1);
}

void KeyboardInput::TapVirtualKey(WORD virtualKey, bool shiftHeld) {
    if (shiftHeld) {
        INPUT inputs[4]{
            MakeKeyInput(VK_SHIFT, 0),
            MakeKeyInput(virtualKey, 0),
            MakeKeyInput(virtualKey, KEYEVENTF_KEYUP),
            MakeKeyInput(VK_SHIFT, KEYEVENTF_KEYUP),
        };
        SendKeyInputs(inputs, 4);
        return;
    }

    TapVirtualKeyPair(virtualKey);
}

void KeyboardInput::TypeCharacter(wchar_t character, bool shiftHeldFlag) {
    if (character == L'\0') {
        return;
    }

    const SHORT mapped = VkKeyScanW(character);
    if (mapped == -1) {
        return;
    }

    const WORD virtualKey = LOBYTE(mapped);
    const bool needsShift = (HIBYTE(mapped) & 1) != 0;
    TapVirtualKey(virtualKey, shiftHeldFlag || needsShift);
}

void KeyboardInput::Backspace() {
    TapVirtualKeyPair(VK_BACK);
}

void KeyboardInput::DiscreteKeyDown(WORD virtualKey) {
    SendDiscreteKey(virtualKey, 0);
}

void KeyboardInput::DiscreteKeyUp(WORD virtualKey) {
    SendDiscreteKey(virtualKey, KEYEVENTF_KEYUP);
}

void KeyboardInput::SendDiscreteKey(WORD virtualKey, DWORD flags) {
    const UINT scanEx = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
    const WORD scanCode = static_cast<WORD>(LOBYTE(scanEx));
    const bool extended = (scanEx & 0x100) != 0;

    DWORD eventFlags = KEYEVENTF_SCANCODE | flags;
    if (extended) {
        eventFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scanCode;
    input.ki.dwFlags = eventFlags;
    SendKeyInputs(&input, 1);
}

void KeyboardInput::ArrowLeft() {
    TapVirtualKeyPair(VK_LEFT);
}

void KeyboardInput::ArrowRight() {
    TapVirtualKeyPair(VK_RIGHT);
}

void KeyboardInput::Enter() {
    TapVirtualKeyPair(VK_RETURN);
}

void KeyboardInput::Space() {
    TapVirtualKeyPair(VK_SPACE);
}

void KeyboardInput::Escape() {
    TapVirtualKeyPair(VK_ESCAPE);
}
