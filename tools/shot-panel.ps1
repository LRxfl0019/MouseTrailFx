param([Parameter(Mandatory=$true)][string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;using System.Text;using System.Runtime.InteropServices;
public static class ShotFx {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  public delegate bool Proc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(Proc p, IntPtr l);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L,T,R,B; }
  public static void Fix() { SetProcessDpiAwarenessContext(new IntPtr(-4)); }
  public static IntPtr Find(string cls) {
    IntPtr f = IntPtr.Zero;
    Proc cb = (h,l) => { var c = new StringBuilder(64); GetClassName(h, c, 64); if (c.ToString() == cls) { f = h; return false; } return true; };
    EnumWindows(cb, IntPtr.Zero);
    return f;
  }
}
"@
[ShotFx]::Fix()
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public static class ShotWin {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
}
"@
$h = [ShotFx]::Find('MTFXPanelWnd')
if ($h -eq [IntPtr]::Zero) { Write-Output "panel not found"; exit 2 }
[void][ShotWin]::ShowWindow($h, 5)
[void][ShotWin]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010)
[void][ShotWin]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400
$r = New-Object ShotFx+RECT
[void][ShotFx]::GetWindowRect($h, [ref]$r)
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $hh))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
[void][ShotWin]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010)
Write-Output ("saved " + $Out + " " + $w + "x" + $hh)

