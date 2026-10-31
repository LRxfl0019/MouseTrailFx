param([string]$Out = "cap.png", [int]$X = -1, [int]$Y = -1, [int]$W = -1, [int]$H = -1)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class DpiFix {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    public static void Fix() { SetProcessDpiAwarenessContext(new IntPtr(-4)); }
}
"@
[DpiFix]::Fix()
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$path = Join-Path (Get-Location) $Out
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
if ($W -gt 0) {
    $rect = New-Object System.Drawing.Rectangle $X, $Y, $W, $H
    $crop = $bmp.Clone($rect, $bmp.PixelFormat)
    $cropPath = Join-Path (Get-Location) ($Out -replace '\.png$', '_crop.png')
    $crop.Save($cropPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $crop.Dispose()
    Write-Output "saved $cropPath"
}
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved $path " + $b.Width + "x" + $b.Height)
