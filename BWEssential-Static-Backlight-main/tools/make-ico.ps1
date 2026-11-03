# Generate app.ico (multi-size Windows icon) from razer.png.
#
#   pwsh tools\make-ico.ps1                      # razer.png -> app.ico
#   pwsh tools\make-ico.ps1 -Source art.png -Destination app.ico
#
# 16/24/32/48 px are written as classic 32bpp DIB frames, 64/128/256 px as PNG
# frames (Vista+ icon format). The result is picked up by app.rc at build time.

param(
    [string]$Source = (Join-Path (Split-Path -Parent $PSScriptRoot) 'razer.png'),
    [string]$Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) 'app.ico')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = [System.Drawing.Bitmap]::new($Source)

# sizes rendered as classic 32bpp DIB (best compatibility at small sizes),
# larger sizes stored as PNG frames (Vista+ icon format).
$dibSizes = @(16, 24, 32, 48)
$pngSizes = @(64, 128, 256)
$entries = New-Object System.Collections.ArrayList

function New-ScaledBitmap {
    param([System.Drawing.Bitmap]$Source2, [int]$Size)
    $dst = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    try {
        $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.DrawImage($Source2, [System.Drawing.Rectangle]::new(0, 0, $Size, $Size),
                     0, 0, $Source2.Width, $Source2.Height, [System.Drawing.GraphicsUnit]::Pixel)
    } finally { $g.Dispose() }
    return $dst
}

function Get-BgraBytes {
    param([System.Drawing.Bitmap]$Bitmap)
    $rect = [System.Drawing.Rectangle]::new(0, 0, $Bitmap.Width, $Bitmap.Height)
    $bd = $Bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = $bd.Stride
        $raw = [byte[]]::new($stride * $Bitmap.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $raw, 0, $raw.Length)
        # repack to tight rows (stride may exceed width*4)
        $rowBytes = $Bitmap.Width * 4
        $tight = [byte[]]::new($rowBytes * $Bitmap.Height)
        for ($y = 0; $y -lt $Bitmap.Height; $y++) {
            [System.Array]::Copy($raw, $y * $stride, $tight, $y * $rowBytes, $rowBytes)
        }
        return ,$tight
    } finally { $Bitmap.UnlockBits($bd) }
}

foreach ($size in $dibSizes) {
    $bmp = New-ScaledBitmap -Source2 $src -Size $size
    try {
        $pixels = Get-BgraBytes -Bitmap $bmp
        $maskRow = [int]([math]::Ceiling($size / 32.0) * 4)
        $mask = [byte[]]::new($maskRow * $size)

        $ms = [System.IO.MemoryStream]::new()
        $bw = [System.IO.BinaryWriter]::new($ms)
        # BITMAPINFOHEADER
        $bw.Write([int]40)          # biSize
        $bw.Write([int]$size)       # biWidth
        $bw.Write([int]($size * 2)) # biHeight (XOR + AND)
        $bw.Write([int16]1)         # biPlanes
        $bw.Write([int16]32)        # biBitCount
        $bw.Write([int]0)           # biCompression
        $bw.Write([int]($size * $size * 4))
        $bw.Write([int]0); $bw.Write([int]0)
        $bw.Write([int]0); $bw.Write([int]0)
        # XOR bitmap, bottom-up
        for ($y = $size - 1; $y -ge 0; $y--) {
            $bw.Write($pixels, $y * $size * 4, $size * 4)
        }
        # AND mask (unused for 32bpp alpha icons, kept zero)
        $bw.Write($mask)
        $bw.Flush()
        [void]$entries.Add([pscustomobject]@{ Size = $size; Data = $ms.ToArray() })
        $bw.Dispose(); $ms.Dispose()
    } finally { $bmp.Dispose() }
}

foreach ($size in $pngSizes) {
    $bmp = New-ScaledBitmap -Source2 $src -Size $size
    try {
        $ms = [System.IO.MemoryStream]::new()
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        [void]$entries.Add([pscustomobject]@{ Size = $size; Data = $ms.ToArray() })
        $ms.Dispose()
    } finally { $bmp.Dispose() }
}
$src.Dispose()

# ICONDIR + ICONDIRENTRY table
$ms = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($ms)
$bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
    $dim = if ($e.Size -ge 256) { 0 } else { $e.Size }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([int16]1); $bw.Write([int16]32)
    $bw.Write([int]$e.Data.Length)
    $bw.Write([int]$offset)
    $offset += $e.Data.Length
}
foreach ($e in $entries) { $bw.Write($e.Data) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($Destination, $ms.ToArray())
$bw.Dispose(); $ms.Dispose()

"wrote $Destination : $((Get-Item $Destination).Length) bytes"
$entries | ForEach-Object { "  {0,3}x{0,-3} {1,7} bytes  {2}" -f $_.Size, $_.Data.Length, $(if ($_.Data[0] -eq 0x89) { 'PNG' } else { 'DIB' }) }
