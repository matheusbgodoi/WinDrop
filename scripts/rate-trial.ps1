# Tries data rates through the installed WinDrop: for each, sets the rate file,
# turns the radio off and on, sends one file to a device and prints the speed
# the engine reports. Someone has to accept each transfer on the device.
#   rate-trial.ps1 -To MacBook -File big.bin -Rates 49,4c,108,8107
param([Parameter(Mandatory)][string]$To, [Parameter(Mandatory)][string]$File,
      [string[]]$Rates = @('49', '4c'), [int]$WaitSeconds = 150)
$ErrorActionPreference = 'Stop'

$data = "$env:ProgramData\winawdl"
$log = "$env:LOCALAPPDATA\winawdl\engine.log"
$app = 'C:\Program Files\winawdl\winawdl.exe'
function Wait-Status([string]$pattern, [int]$seconds) {
  foreach ($i in 1..$seconds) {
    if ((Get-Content "$data\logs\radio.status" -ErrorAction SilentlyContinue) -match $pattern) { return $true }
    Start-Sleep -Seconds 1
  }
  $false
}

foreach ($rate in $Rates) {
  Start-Process $app; Start-Sleep -Seconds 4   # brings the window up, or starts the app
  if (Get-Process radiod -ErrorAction SilentlyContinue) {
    & "$PSScriptRoot\app-radio-switch.ps1" -Off | Out-Null
    [void](Wait-Status 'done' 40)
  }
  Set-Content "$data\radio.rate" $rate -NoNewline
  & "$PSScriptRoot\app-radio-switch.ps1" | Out-Null
  if (-not (Wait-Status 'running' 60)) { "rate 0x${rate}: the radio did not start"; continue }

  $from = (Get-Content $log).Count
  $sent = $false
  foreach ($try in 1..12) {            # until the device shows up in the window
    Start-Sleep -Seconds 5
    try { & "$PSScriptRoot\app-send.ps1" -To $To $File | Out-Null; $sent = $true; break } catch { }
  }
  if (-not $sent) { "rate 0x${rate}: $To never appeared"; continue }
  $result = $null
  foreach ($i in 1..$WaitSeconds) {
    Start-Sleep -Seconds 1
    $result = Get-Content $log | Select-Object -Skip $from |
      Where-Object { $_ -match 'sent, at|declined|broke|did not answer|failed' } | Select-Object -Last 1
    if ($result) { break }
  }
  "rate 0x${rate}: $(if ($result) { ($result -split ' ', 4)[3] } else { 'no result in time' })"
}
Remove-Item "$data\radio.rate" -ErrorAction SilentlyContinue
