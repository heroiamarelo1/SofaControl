#pragma once

// SofaControl operating modes.
enum class AppMode {
    VirtualMouse,  // Left stick moves mouse; A/B click; controller hidden via HidHide
    Passthrough    // Native XInput passthrough: HidHide off, game reads real hardware directly
};
