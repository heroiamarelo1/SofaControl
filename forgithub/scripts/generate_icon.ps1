# Builds assets/SofaControl.ico from assets/app_icon.png.
# The near-black background is keyed out to transparency, then the image is
# emitted as a Vista+ .ico with PNG-compressed entries at multiple sizes so it
# stays crisp from the 16x16 tray up to 256x256 Explorer views.
param(
    [string]$Source = (Join-Path $PSScriptRoot "..\assets\app_icon.png"),
    [string]$Output = (Join-Path $PSScriptRoot "..\assets\SofaControl.ico"),
    [int]$BlackThreshold = 45
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$Source = [System.IO.Path]::GetFullPath($Source)
$Output = [System.IO.Path]::GetFullPath($Output)

if (-not (Test-Path $Source)) {
    throw "Source image not found: $Source"
}

$src = [System.Drawing.Bitmap]::FromFile($Source)

# Master 256x256 ARGB canvas (largest ICO entry); all sizes derive from it.
$master = New-Object System.Drawing.Bitmap(256, 256, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$mg = [System.Drawing.Graphics]::FromImage($master)
$mg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$mg.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$mg.Clear([System.Drawing.Color]::Transparent)
$mg.DrawImage($src, 0, 0, 256, 256)
$mg.Dispose()
$src.Dispose()

# Key out the near-black backdrop -> fully transparent (BGRA byte order).
$rect = New-Object System.Drawing.Rectangle(0, 0, 256, 256)
$data = $master.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $data.Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
for ($i = 0; $i -lt $bytes.Length; $i += 4) {
    $b = $bytes[$i]; $g = $bytes[$i + 1]; $r = $bytes[$i + 2]
    if ($b -lt $BlackThreshold -and $g -lt $BlackThreshold -and $r -lt $BlackThreshold) {
        $bytes[$i + 3] = 0  # alpha
    }
}
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
$master.UnlockBits($data)

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngBlobs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($master, 0, 0, $s, $s)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngBlobs += ,@{ Size = $s; Bytes = $ms.ToArray() }
    $ms.Dispose()
}
$master.Dispose()

$fs = New-Object System.IO.FileStream($Output, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)

# ICONDIR
$bw.Write([UInt16]0)               # reserved
$bw.Write([UInt16]1)               # type = icon
$bw.Write([UInt16]$pngBlobs.Count) # image count

$offset = 6 + (16 * $pngBlobs.Count)
foreach ($b in $pngBlobs) {
    $dim = if ($b.Size -ge 256) { 0 } else { $b.Size }  # 0 means 256 in ICO
    $bw.Write([Byte]$dim)          # width
    $bw.Write([Byte]$dim)          # height
    $bw.Write([Byte]0)             # palette
    $bw.Write([Byte]0)             # reserved
    $bw.Write([UInt16]1)           # color planes
    $bw.Write([UInt16]32)          # bits per pixel
    $bw.Write([UInt32]$b.Bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $b.Bytes.Length
}
foreach ($b in $pngBlobs) {
    $bw.Write($b.Bytes)
}

$bw.Flush()
$bw.Close()
$fs.Close()

Write-Host "Wrote $Output ($($pngBlobs.Count) sizes, black<$BlackThreshold keyed out)."
