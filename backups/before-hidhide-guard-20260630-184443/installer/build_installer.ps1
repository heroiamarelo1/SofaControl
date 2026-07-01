# SofaControl - build the distributable installer (single .exe).
#
#   .\installer\build_installer.ps1            # build app + compile installer
#   .\installer\build_installer.ps1 -SkipBuild # only compile installer
#
# Output: installer\Output\SofaControl-Setup.exe

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072

$InstallerDir = $PSScriptRoot
$ProjectRoot  = Split-Path $InstallerDir -Parent
$RedistDir    = Join-Path $InstallerDir "redist"

# --- 1. Build the application ------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "Building SofaControl..." -ForegroundColor Cyan
    & (Join-Path $ProjectRoot "build.ps1")
    if ($LASTEXITCODE -ne 0) { throw "App build failed." }
}

$Exe = Join-Path $ProjectRoot "build\Release\SofaControl.exe"
if (-not (Test-Path $Exe)) { throw "SofaControl.exe not found. Run build.ps1 first." }

# --- 2. Prepare installer-only assets ---------------------------------------
$CommandListPng = Join-Path $ProjectRoot "assets\command_list.png"
$CommandListBmp = Join-Path $InstallerDir "command_list_installer.bmp"
if (-not (Test-Path $CommandListPng)) { throw "Command list image not found: $CommandListPng" }

Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new($CommandListPng)
try {
    $bitmap.Save($CommandListBmp, [System.Drawing.Imaging.ImageFormat]::Bmp)
} finally {
    $bitmap.Dispose()
}

# --- 3. Ensure the redistributable drivers are present ----------------------
$Drivers = @{
    "ViGEmBus_1.22.0_x64_x86_arm64.exe" = "https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe"
    "HidHide_1.5.230_x64.exe"           = "https://github.com/nefarius/HidHide/releases/download/v1.5.230.0/HidHide_1.5.230_x64.exe"
}
New-Item -ItemType Directory -Force -Path $RedistDir | Out-Null
foreach ($name in $Drivers.Keys) {
    $dest = Join-Path $RedistDir $name
    if (-not (Test-Path $dest)) {
        Write-Host "Downloading $name..." -ForegroundColor Yellow
        Invoke-WebRequest -Uri $Drivers[$name] -OutFile $dest
    }
}

# --- 4. Locate the Inno Setup compiler --------------------------------------
$IsccCandidates = @(
    "iscc",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
)
$Iscc = $null
foreach ($c in $IsccCandidates) {
    if ($c -eq "iscc") {
        if (Get-Command iscc -ErrorAction SilentlyContinue) { $Iscc = "iscc"; break }
    } elseif (Test-Path $c) {
        $Iscc = $c; break
    }
}
if (-not $Iscc) {
    throw "Inno Setup (ISCC.exe) not found. Install it with: winget install JRSoftware.InnoSetup"
}

# --- 5. Compile the installer ----------------------------------------------
Write-Host "Compiling installer with: $Iscc" -ForegroundColor Cyan
& $Iscc (Join-Path $InstallerDir "SofaControl.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }

$Setup = Join-Path $InstallerDir "Output\SofaControl-Setup.exe"
Write-Host "Installer ready: $Setup" -ForegroundColor Green
