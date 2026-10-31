param([string]$Out = "app.ico")
Add-Type -AssemblyName System.Drawing

function New-IconBitmap([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $c = ($s - 1) / 2.0
    for ($y = 0; $y -lt $s; $y++) {
        for ($x = 0; $x -lt $s; $x++) {
            $dx = $x - $c; $dy = $y - $c
            $d = [math]::Sqrt($dx * $dx + $dy * $dy) / ($s / 2.0)
            $a = 1.0 - $d
            if ($a -lt 0) { $a = 0 }
            $a = [math]::Pow($a, 1.5) * 1.25
            if ($a -gt 1) { $a = 1 }
            $t = 1.0 - [math]::Min(1.0, $d * 1.7)
            $r = 0.13 + (1.0 - 0.13) * $t * 0.9
            $g = 0.78 + (1.0 - 0.78) * $t * 0.9
            $b = 0.84 + (1.0 - 0.84) * $t * 0.9
            $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(
                [int]($a * 255), [int]($r * 255), [int]($g * 255), [int]($b * 255)))
        }
    }
    return $bmp
}

$sizes = @(16, 32, 48, 64, 256)
$entries = @()
$payloads = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    if ($s -ge 256) {
        # 大尺寸用 PNG 压缩（Vista+ 支持 ICO 内嵌 PNG），避免 262KB 的裸位图
        $png = New-Object System.IO.MemoryStream
        $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $payloads += ,$png.ToArray()
        $entries += ,@($s, $png.Length)
        $png.Dispose(); $bmp.Dispose()
        continue
    }
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    # BITMAPINFOHEADER (height doubled, bottom-up)
    $bw.Write([uint32]40); $bw.Write([int32]$s); $bw.Write([int32]($s * 2))
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]0)
    $bw.Write([uint32]($s * $s * 4)); $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    # XOR: BGRA, bottom-up
    for ($y = $s - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $s; $x++) {
            $p = $bmp.GetPixel($x, $y)
            $bw.Write([byte]$p.B); $bw.Write([byte]$p.G); $bw.Write([byte]$p.R); $bw.Write([byte]$p.A)
        }
    }
    # AND mask: all zero (alpha used), rows padded to 32-bit
    $maskRow = [math]::Ceiling($s / 32.0) * 4
    $zero = New-Object byte[] $maskRow
    for ($y = 0; $y -lt $s; $y++) { $bw.Write($zero) }
    $bw.Flush()
    $payloads += ,$ms.ToArray()
    $entries += ,@($s, $ms.Length)
    $bw.Dispose(); $bmp.Dispose()
}

$fs = [System.IO.File]::Create((Join-Path (Get-Location) $Out))
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $entries[$i][0]; $len = $entries[$i][1]
    $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$len); $w.Write([uint32]$offset)
    $offset += $len
}
foreach ($p in $payloads) { $w.Write($p) }
$w.Flush(); $w.Dispose(); $fs.Dispose()
Write-Output ("ico written: " + $Out + " " + (Get-Item (Join-Path (Get-Location) $Out)).Length + " bytes")
