# Saves a picture of the winawdl window alone, to check the app without a
# person at the screen.   app-shot.ps1 <out.png>
param([Parameter(Mandatory)][string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WinShot {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w, out Rect r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr w, IntPtr dc, uint flags);
}
'@
$window = (Get-Process winawdl -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 |
           Select-Object -First 1).MainWindowHandle
if (-not $window) { throw 'The winawdl window is not open.' }
$rect = New-Object WinShot+Rect
[void][WinShot]::GetWindowRect($window, [ref]$rect)
$bitmap = New-Object Drawing.Bitmap ($rect.R - $rect.L), ($rect.B - $rect.T)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
[void][WinShot]::PrintWindow($window, $dc, 2) # PW_RENDERFULLCONTENT
$graphics.ReleaseHdc($dc)
$bitmap.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
"saved $Out ($($bitmap.Width)x$($bitmap.Height))"
