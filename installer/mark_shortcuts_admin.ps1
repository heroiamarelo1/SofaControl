param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ShortcutPaths
)

$ErrorActionPreference = "SilentlyContinue"

foreach ($path in $ShortcutPaths) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) {
        continue
    }

    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -le 0x15) {
        continue
    }

    # Set SLDF_RUNAS_USER in the Shell Link header.
    $bytes[0x15] = $bytes[0x15] -bor 0x20
    [System.IO.File]::WriteAllBytes($path, $bytes)
}
