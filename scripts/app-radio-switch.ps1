# Presses the Radio switch of the running winawdl window through UI
# Automation, as a person's click would, and says what state it is left in.
#   app-radio-switch.ps1 [-Off]
param([switch]$Off)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes

$process = Get-Process winawdl -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
if (-not $process) { throw 'The winawdl window is not open.' }
$window = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$isToggle = New-Object Windows.Automation.PropertyCondition(
    [Windows.Automation.AutomationElement]::IsTogglePatternAvailableProperty, $true)
$switch = $window.FindFirst([Windows.Automation.TreeScope]::Descendants, $isToggle)
if (-not $switch) { throw 'No switch was found in the window.' }
$toggle = $switch.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
$wanted = if ($Off) { 'Off' } else { 'On' }
if ("$($toggle.Current.ToggleState)" -ne $wanted) { $toggle.Toggle() }
Start-Sleep -Milliseconds 500
"switch '$($switch.Current.Name)' is $($toggle.Current.ToggleState)"
