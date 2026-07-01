# SofaControl GitHub upload notes

This folder is a clean source snapshot prepared for GitHub.

Included:
- source code in `src/`
- assets needed by the app in `assets/`
- installer scripts in `installer/`
- README, LICENSE, build script, CMake project, .gitignore

Not included:
- compiled build output
- installer output
- bundled driver redistributables in `installer/redist/`
- third-party build folders

To rebuild locally:

```powershell
.\build.ps1
.\installer\build_installer.ps1
```

The installer build script can recreate/download the redistributables when needed.
