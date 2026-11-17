# Saves a picture of a file's right-click menu in Explorer, to show "Enviar via
# AirDrop" in the documentation. It opens the samples folder in a window of
# its own, finds the file through UI Automation and right-clicks it with Shift
# held (the full menu). Nothing is clicked unless the point belongs to that
# very window. Only the pane of files is in the picture: the navigation pane
# and whatever is behind the window show the person's own folders.
#   menu-shot.ps1 <file> <out.png>
param([Parameter(Mandatory)][string]$File, [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MenuMouse {
  [StructLayout(LayoutKind.Sequential)] public struct Pt { public int X, Y; }
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Pt p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr w, uint kind);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr w, int x, int y, int cx, int cy, bool paint);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
  public static IntPtr RootAt(int x, int y) { return GetAncestor(WindowFromPoint(new Pt { X = x, Y = y }), 2); }
}
'@
$left = 40; $top = 20; $width = 1100; $height = 1000
$paneLeft = $left + 214; $paneTop = $top + 132
$path = (Resolve-Path $File).Path

$shell = New-Object -ComObject Shell.Application
$before = @($shell.Windows() | ForEach-Object { $_.HWND })
Start-Process explorer.exe "`"$(Split-Path $path -Parent)`""
$window = $null
foreach ($try in 1..40) {
  Start-Sleep -Milliseconds 250
  $window = $shell.Windows() | Where-Object { $before -notcontains $_.HWND } | Select-Object -First 1
  if ($window) { break }
}
if (-not $window) { throw 'Explorer did not open the folder.' }
$shift = 0x10; $keyUp = 2; $leftDown = 2; $leftUp = 4; $rightDown = 8; $rightUp = 0x10
try {
  $handle = [IntPtr]$window.HWND
  [void][MenuMouse]::MoveWindow($handle, $left, $top, $width, $height, $true)
  Start-Sleep -Seconds 2
  $named = New-Object Windows.Automation.PropertyCondition(
      [Windows.Automation.AutomationElement]::NameProperty, (Split-Path $path -Leaf))
  $entry = [Windows.Automation.AutomationElement]::FromHandle($handle).FindFirst(
      [Windows.Automation.TreeScope]::Descendants, $named)
  if (-not $entry) { throw 'The file is not in the window.' }
  $box = $entry.Current.BoundingRectangle
  $x = [int]($box.Left + $box.Width / 2); $y = [int]($box.Top + $box.Height / 3)
  if ([MenuMouse]::RootAt($x, $y) -ne $handle) { throw 'Another window covers the file: nothing was clicked.' }

  [void][MenuMouse]::SetCursorPos($x, $y)
  Start-Sleep -Milliseconds 200
  [MenuMouse]::keybd_event($shift, 0, 0, [UIntPtr]::Zero)
  [MenuMouse]::mouse_event($rightDown, 0, 0, 0, [UIntPtr]::Zero)
  [MenuMouse]::mouse_event($rightUp, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 150
  [MenuMouse]::keybd_event($shift, 0, $keyUp, [UIntPtr]::Zero)
  Start-Sleep -Seconds 2

  $bitmap = New-Object Drawing.Bitmap ($left + $width - $paneLeft - 8), ($top + $height - $paneTop - 40)
  $graphics = [Drawing.Graphics]::FromImage($bitmap)
  $graphics.CopyFromScreen($paneLeft, $paneTop, 0, 0, $bitmap.Size)
  $bitmap.Save($Out, [Drawing.Imaging.ImageFormat]::Png)

  # Closes the menu: a click on empty space of the same pane, if it is still that window there.
  $awayX = $left + $width - 60; $awayY = $top + $height - 120
  if ([MenuMouse]::RootAt($awayX, $awayY) -eq $handle) {
    [void][MenuMouse]::SetCursorPos($awayX, $awayY)
    [MenuMouse]::mouse_event($leftDown, 0, 0, 0, [UIntPtr]::Zero)
    [MenuMouse]::mouse_event($leftUp, 0, 0, 0, [UIntPtr]::Zero)
  }
  Start-Sleep -Milliseconds 500
  "saved $Out ($($bitmap.Width)x$($bitmap.Height))"
} finally {
  [MenuMouse]::keybd_event($shift, 0, $keyUp, [UIntPtr]::Zero)
  $window.Quit()
}
