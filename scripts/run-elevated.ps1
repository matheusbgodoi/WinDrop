# Runs a program elevated and prints what it wrote, for shells that are not.
#   run-elevated.ps1 [-AsSystem] <exe> [args...]
# -AsSystem runs it as LocalSystem through a one-off scheduled task: that
# account holds SeLockMemoryPrivilege, which the DMA slab needs and an
# administrator account lacks until it is granted "Lock pages in memory".
param([switch]$AsSystem,
      [Parameter(Mandatory, Position = 0)][string]$Exe,
      [Parameter(ValueFromRemainingArguments)][string[]]$Rest)
$ErrorActionPreference = 'Stop'

$stem = Join-Path $env:TEMP "winawdl-elevated-$PID"
$log = "$stem.log"
$quoted = ($Rest | ForEach-Object { '"' + $_ + '"' }) -join ' '

# What finally runs the program, as whichever account.
"& `"$Exe`" $quoted *> `"$log`"`r`n`"exit code `$LASTEXITCODE`" >> `"$log`"" |
  Set-Content -Encoding utf8 "$stem-inner.ps1"
$inner = "powershell -NoProfile -ExecutionPolicy Bypass -File `"$stem-inner.ps1`""

if ($AsSystem) {
  $task = "winawdl-elevated-$PID"
  @"
schtasks /create /tn $task /ru SYSTEM /sc once /st 00:00 /f /tr '$inner' | Out-Null
schtasks /run /tn $task | Out-Null
do { Start-Sleep -Milliseconds 300 } while ((schtasks /query /tn $task /fo csv | ConvertFrom-Csv).Status -notin 'Ready', 'Pronto')
schtasks /delete /tn $task /f | Out-Null
"@ | Set-Content -Encoding utf8 "$stem-outer.ps1"
} else {
  $inner | Set-Content -Encoding utf8 "$stem-outer.ps1"
}

Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList `
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$stem-outer.ps1"
if (Test-Path $log) { Get-Content $log }
Remove-Item "$stem*" -ErrorAction SilentlyContinue
