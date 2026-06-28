# Gera assets/toggle.wav (som curto ao alternar modo)
$dir = Join-Path $PSScriptRoot "..\assets"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$sampleRate = 22050
$duration = 0.18
$frequency = 880.0
$numSamples = [int]($sampleRate * $duration)
$amplitude = 12000

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)

[void]$bw.Write([char[]]@('R','I','F','F'))
[void]$bw.Write([int]0)
[void]$bw.Write([char[]]@('W','A','V','E'))
[void]$bw.Write([char[]]@('f','m','t',' '))
[void]$bw.Write([int]16)
[void]$bw.Write([Int16]1)
[void]$bw.Write([Int16]1)
[void]$bw.Write([int]$sampleRate)
[void]$bw.Write([int]($sampleRate * 2))
[void]$bw.Write([Int16]2)
[void]$bw.Write([Int16]16)
[void]$bw.Write([char[]]@('d','a','t','a'))
[void]$bw.Write([int]($numSamples * 2))

for ($i = 0; $i -lt $numSamples; $i++) {
    $t = $i / $sampleRate
    $envelope = [Math]::Min(1.0, ($numSamples - $i) / ($sampleRate * 0.05))
    $sample = [Int16]([Math]::Sin(2 * [Math]::PI * $frequency * $t) * $amplitude * $envelope)
    [void]$bw.Write($sample)
}

$bytes = $ms.ToArray()
$fileSize = $bytes.Length - 8
$bytes[4] = [byte]($fileSize -band 0xFF)
$bytes[5] = [byte](($fileSize -shr 8) -band 0xFF)
$bytes[6] = [byte](($fileSize -shr 16) -band 0xFF)
$bytes[7] = [byte](($fileSize -shr 24) -band 0xFF)

$outPath = Join-Path $dir "toggle.wav"
[System.IO.File]::WriteAllBytes($outPath, $bytes)
Write-Host "Criado: $outPath ($($bytes.Length) bytes)"
