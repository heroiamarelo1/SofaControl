# 🛋️ SofaControl: PC gaming, sofa style.

Transform your PC into a console experience. No complex setups—just plug and play. 🎮 Works with XINPUT controllers.

## 🚀 Key Features

* **Plug-and-Play:** Zero configuration required. Ready to go instantly. ✅
* **Instant Toggle:** Switch between **Mouse Mode** and **Native Gamepad** (or emulated XBOX360) instantly with `LT + RT + B + Y`. 🔄
* **Precision Mode:** Hold `RT` in Mouse Mode for surgical control—perfect for navigating desktop UI with ease. 🎯
* **Total Control:** Manage your entire library, browser, and media from the couch. Close apps, use the on-screen keyboard, shutdown your PC with simple combos. 🕹️

---

## 📋 Command Map
![Command List](command_list_installer.jpg)

---

*Support development by [buying me a coffee (5€)](https://paypal.me/heroiamarelo/5)! ☕*

**Use your Xbox controller as a mouse + keyboard for your living-room PC - no
keyboard or mouse needed on the couch.**

SofaControl turns an Xbox controller into a virtual mouse and on-screen keyboard
so you can drive a home-theater / living-room PC from the sofa: browse, type,
watch, and launch games. One combo flips the same controller into game mode.

SofaControl is free and **open source** (MIT). It builds on two open-source,
signed drivers by Nefarius Software Solutions:

- **[HidHide](https://github.com/nefarius/HidHide)** - hides your physical
  controller from games and Windows shell input so only SofaControl reads it in
  mouse mode.
- **[ViGEmBus](https://github.com/nefarius/ViGEmBus)** - lets SofaControl
  expose a virtual Xbox 360 controller to games when the optional
  "Emulated 360" backend is selected.

## Install

Download and run **`SofaControl-Setup.exe`**. It is a single installer that
bundles:

- the SofaControl app and assets,
- the **ViGEmBus** and **HidHide** drivers, skipped if already present,
- the Command List image inside the installer,
- an optional **"Turn on when Windows starts"** checkbox,
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
  game mode. The default backend is **Native Xbox Controller** with HidHide.
  **Emulated Xbox 360** via ViGEmBus remains available as an optional backend.
- **Pre-arm** - double-tap A in mouse mode to briefly reveal the controller so
  a game can detect it.
- **System combos**:
  - **LT + RT + START** (hold 2 s) - close the foreground app/game.
  - **LT + RT + SELECT** (hold 2 s) - run the configured power action.
- **Wake the PC** - optionally let the controller wake the PC from supported
  sleep states.
- **Tray app** - starts in the system tray; the in-app **Command List** shows
  every mapping.

## Controller Hiding Behavior

SofaControl only reacts to the first XInput slot. HidHide paths are remembered
so the same controller can be hidden before Windows fully picks it up on the
next boot or reconnect.

If the remembered controller is gone and a new controller appears in the first
XInput slot, SofaControl learns that new controller and replaces the remembered
HidHide paths. It does not intentionally manage two controllers at the same
time.

To revert this remembered-path behavior without removing the rest of the app,
set `RememberControllerPaths=0` under `[SofaControl]` in
`%APPDATA%\SofaControl\config.ini`, then restart SofaControl.

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
Physical Xbox controller -> HidHide -> SofaControl
                                   -> virtual mouse / keyboard
                                   -> optional ViGEmBus virtual Xbox 360
```

## License

SofaControl was created by **HeroiAmarelo** and is released under the **MIT
License**. The bundled **ViGEmBus**, **HidHide**, and **ViGEmClient** components
are BSD-3-Clause, copyright Nefarius Software Solutions e.U.
