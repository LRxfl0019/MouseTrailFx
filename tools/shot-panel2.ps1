param([Parameter(Mandatory=$true)][string]$Out)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System;using System.Text;
using System.Runtime.InteropServices;
public static class SP {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
    public delegate bool Proc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Proc p, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    public static IntPtr FindByClass(string cls) {
        IntPtr f = IntPtr.Zero;
        Proc cb = (h, l) => { var s = new StringBuilder(64); GetClassName(h, s, 64); if (s.ToString() == cls) { f = h; return false; } return true; };
        EnumWindows(cb, IntPtr.Zero);
        return f;
    }
    public static void Fix() { SetProcessDpiAwarenessContext(new IntPtr(-4)); }
}
"@
[SP]::Fix()
$h = [SP]::FindByClass('MTFXPanelWnd')
if ($h -eq [IntPtr]::Zero) { Write-Output 'panel not found'; exit 2 }
[void][SP]::ShowWindow($h, 5)
[void][SP]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010)
[void][SP]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 500

$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
Write-Output ("screen " + $b.Width + "x" + $b.Height)
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)

# 闈㈡澘鎸夎璁″眳涓細瀹?470*scale锛岄珮 820*scale锛堝姞鏍囬鏍忥級
$scale = $b.Height / 1600.0
$pw = [int](705 * $scale); $ph = [int](1275 * $scale)
$px = [int](($b.Width - $pw) / 2); $py = [int](($b.Height - $ph) / 2)
$rect = New-Object System.Drawing.Rectangle $px, $py, $pw, $ph
$crop = $bmp.Clone($rect, $bmp.PixelFormat)
$crop.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$crop.Dispose(); $g.Dispose(); $bmp.Dispose()
[void][SP]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010)
Write-Output ("saved " + $Out + " " + $pw + "x" + $ph + " at " + $px + "," + $py)

