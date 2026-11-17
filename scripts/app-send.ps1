# Hands files to the running WinDrop, as "Enviar via AirDrop" in Explorer
# does, and presses the device whose name holds -To, through UI Automation.
#   app-send.ps1 -To MacBook file1 [file2...]
param([Parameter(Mandatory)][string]$To,
      [Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Files)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes

$process = Get-Process winawdl -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
if (-not $process) { throw 'The WinDrop window is not open.' }
& $process.Path @($Files | ForEach-Object { (Resolve-Path $_).Path })
Start-Sleep -Seconds 2

$window = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$all = $window.FindAll([Windows.Automation.TreeScope]::Descendants,
                       [Windows.Automation.Condition]::TrueCondition)
$device = $all | Where-Object {
  $_.Current.Name -like "*$To*" -and
  $_.GetCurrentPropertyValue([Windows.Automation.AutomationElement]::IsInvokePatternAvailableProperty)
} | Select-Object -First 1
if (-not $device) { throw "No device named like '$To' can be pressed in the window." }
$device.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
"pressed '$($device.Current.Name)'"
