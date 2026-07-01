# SofaControl

Created by **HeroiAmarelo**.

**Use your Xbox controller as a mouse + keyboard for your living-room PC - no
keyboard or mouse needed on the couch.**

SofaControl turns an Xbox controller into a virtual mouse and on-screen keyboard
so you can drive a home-theater / living-room PC from the sofa: browse, type,
watch, and launch games. One combo flips the same controller into game mode.

SofaControl is free and **open source** (MIT). It can optionally use two
open-source, signed drivers by Nefarius Software Solutions:

- **[HidHide](https://github.com/nefarius/HidHide)** - optionally hides your
  physical controller when the pre-arm feature is enabled.
- **[ViGEmBus](https://github.com/nefarius/ViGEmBus)** - lets SofaControl
  expose a virtual Xbox 360 controller to games when the optional
  "Emulated 360" backend is selected.

## Install

Download and run **`SofaControl-Setup.exe`**. It is a single installer that
bundles:

- the SofaControl app and assets,
- the **ViGEmBus** and **HidHide** drivers, skipped if already present,
- the Command List image inside the installer,
- automatic startup with Windows,
- an optional desktop shortcut.

A reboot may be requested the first time the drivers are installed.

> The installer needs administrator rights because it installs system drivers.

## Features

- **Virtual Mouse mode** - left stick moves the cursor; A = left click,
  B = right click, RT = precision, stick Y = scroll, RB + stick Y = Ctrl+zoom.
- **On-screen keyboard** - press X to type without a real keyboard.
- **Search** - press Y to search and open files from the couch.
- **Go to Desktop** - hold LB for 1 second in mouse mode.
- **Game mode** - **LT + RT + B + Y** hot-switches between virtual mouse and
  game mode. The default backend is **Native Xbox Controller**.
  **Emulated Xbox 360** via ViGEmBus remains available as an optional backend.
- **Pre-arm** - optional. When enabled, the controller is hidden in mouse mode;
  double-tap A briefly reveals it so a game can detect it.
- **System combos**:
  - **LT + RT + START** (hold 2 s) - close the foreground app/game.
  - **LT + RT + SELECT** (hold 2 s) - run the configured power action.
- **Wake the PC** - optionally let the controller wake the PC from supported
  sleep states.
- **Tray app** - starts in the system tray; the in-app **Command List** shows
  every mapping.

## Controller Hiding Behavior

By default SofaControl leaves the physical controller visible. HidHide is only
used when **Enable pre-arm** is turned on. In that mode, the controller is hidden
while SofaControl is acting as a mouse, double-tap A reveals it temporarily, and
it is hidden again if game mode is not selected before the pre-arm timeout.

SofaControl only reacts to the first XInput slot and does not intentionally
manage two controllers at the same time.

## Wake Support

The wake option depends on the PC firmware, USB/controller hardware, Windows
power plan, and the sleep state being used. SofaControl enables matching
wake-capable controller devices and disables USB selective suspend for the
current power plan, but it cannot force wake support on a PC that does not
expose it. Full power-off wake also depends on motherboard support.

## Build From Source

Requires Visual Studio with **Desktop development with C++** and CMake.

```powershell
.\build.ps1
```

Output: `build\Release\SofaControl.exe`.

## Build The Installer

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```powershell
.\installer\build_installer.ps1
```

This builds the app, downloads the drivers into `installer\redist\` if needed,
and compiles **`installer\Output\SofaControl-Setup.exe`**.

## Architecture

```text
Physical Xbox controller -> SofaControl -> virtual mouse / keyboard
                              optional -> HidHide pre-arm hiding
                              optional -> ViGEmBus virtual Xbox 360
```

## License

SofaControl was created by **HeroiAmarelo** and is released under the **MIT
License**. The bundled **ViGEmBus**, **HidHide**, and **ViGEmClient** components
are BSD-3-Clause, copyright Nefarius Software Solutions e.U.
