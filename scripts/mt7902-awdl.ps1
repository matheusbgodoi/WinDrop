# Starts or stops AWDL on the MT7902: the radio daemon and, on top of it, the
# AWDL daemon, both as LocalSystem through the runner task
# (scripts\system-runner.ps1 -Install). Needs mt7902\build.ps1 and
# scripts\build-awdl.ps1 done.
#   mt7902-awdl.ps1 [-Channel 149] [-Seconds 3600] [-Detail] [-DataRate 4c]
#   mt7902-awdl.ps1 -Stop
# The radio daemon ends by itself after -Seconds, so a forgotten session does
# not hold the card forever; 0 means until -Stop.
param([ValidateSet(6, 44, 149)][int]$Channel = 149, [int]$Seconds = 3600,
      [switch]$Detail, [switch]$Stop, [string]$DataRate)
$ErrorActionPreference = 'Stop'

$repo = Split-Path $PSScriptRoot -Parent
$build = "$repo\mt7902\build"
$firmware = "$repo\ref\mt76\firmware"
$run = "$PSScriptRoot\run-system.ps1"

# The AWDL daemon leaves when its radio does.
& $run radiod stop | Select-Object -First 1
foreach ($try in 1..100) {
  if (-not (Get-Process awdld, radiod -ErrorAction SilentlyContinue)) { break }
  Start-Sleep -Milliseconds 100
}
if ($Stop) { return }

Copy-Item "$repo\build\awdl\awdld.exe", "$repo\deps\wintun\wintun.dll" $build -Force
$radioArgs = @("$firmware\WIFI_MT7902_patch_mcu_1_1_hdr.bin",
               "$firmware\WIFI_RAM_CODE_MT7902_1.bin", "$Channel")
$radioArgs += "$Seconds"
if ($DataRate) { $radioArgs += $DataRate }
& $run detach radiod @radioArgs | Out-Null
$awdlArgs = @('-c', "$Channel")
if ($Detail) { $awdlArgs += '-v' }
& $run detach awdld @awdlArgs | Out-Null

foreach ($try in 1..60) {
  $adapter = Get-NetAdapter -Name awdl0 -ErrorAction SilentlyContinue
  if ($adapter -and $adapter.Status -eq 'Up') {
    "AWDL is up on channel $Channel (interface awdl0, index $($adapter.ifIndex))."
    return
  }
  Start-Sleep -Milliseconds 500
}
Get-Content "$build\radiod.log", "$build\awdld.log" -ErrorAction SilentlyContinue |
  Select-Object -Last 6
throw 'AWDL did not come up.'
