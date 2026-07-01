# SofaControl — script de compilação rápida (PowerShell)
# Uso: .\build.ps1          → compila Release
#      .\build.ps1 -Debug   → compila Debug
#      .\build.ps1 -Run     → compila e executa

param(
    [switch]$Debug,
    [switch]$Run
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$BuildDir    = Join-Path $ProjectRoot "build"
$Config      = if ($Debug) { "Debug" } else { "Release" }

# ViGEmClient vendored dependency (BSD-3-Clause)
$ViGEmDir = Join-Path $ProjectRoot "third_party\ViGEmClient"
if (-not (Test-Path (Join-Path $ViGEmDir "CMakeLists.txt"))) {
    Write-Host "A transferir ViGEmClient..." -ForegroundColor Yellow
    $zip = Join-Path $env:TEMP "ViGEmClient.zip"
    Invoke-WebRequest -Uri "https://github.com/nefarius/ViGEmClient/archive/refs/heads/master.zip" -OutFile $zip
    $thirdParty = Join-Path $ProjectRoot "third_party"
    New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null
    Expand-Archive -Path $zip -DestinationPath $thirdParty -Force
    Remove-Item -Recurse -Force $ViGEmDir -ErrorAction SilentlyContinue
    Move-Item (Join-Path $thirdParty "ViGEmClient-master") $ViGEmDir -Force
    Remove-Item $zip -ErrorAction SilentlyContinue
}

# Gerar som de toggle se ainda nao existir
$ToggleWav = Join-Path $ProjectRoot "assets\toggle.wav"
if (-not (Test-Path $ToggleWav)) {
    $PsScript = Join-Path $ProjectRoot "scripts\generate_toggle_wav.ps1"
    if (Test-Path $PsScript) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $PsScript
    }
}

# Detetar CMake (PATH ou Visual Studio)
$CmakeCandidates = @(
    "cmake",
    "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
$CmakeExe = $null
foreach ($c in $CmakeCandidates) {
    if ($c -eq "cmake") {
        if (Get-Command cmake -ErrorAction SilentlyContinue) { $CmakeExe = "cmake"; break }
    } elseif (Test-Path $c) {
        $CmakeExe = $c
        break
    }
}
if (-not $CmakeExe) {
    Write-Error "CMake nao encontrado. Instala Visual Studio com 'Desenvolvimento para Desktop com C++'."
}

# Detetar gerador Visual Studio instalado (2022 ou 2026)
$Generators = @(
    "Visual Studio 18 2026",
    "Visual Studio 17 2022"
)

$Generator = $null
foreach ($g in $Generators) {
    $test = & $CmakeExe -G $g -A x64 -S $ProjectRoot -B $BuildDir 2>&1
    if ($LASTEXITCODE -eq 0) {
        $Generator = $g
        break
    }
    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
}

if (-not $Generator) {
    Write-Error @"
CMake nao encontrou Visual Studio.
Confirma que a instalacao terminou e que marcaste 'Desenvolvimento para Desktop com C++'.
"@
}

Write-Host "Gerador: $Generator | Config: $Config" -ForegroundColor Cyan

& $CmakeExe --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Exe = Join-Path $BuildDir "$Config\SofaControl.exe"
Write-Host "Compilado: $Exe" -ForegroundColor Green

if ($Run) {
    Write-Host "A executar... (Ctrl+C para sair)" -ForegroundColor Yellow
    & $Exe
}
