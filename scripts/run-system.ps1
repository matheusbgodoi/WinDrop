# Runs one of the programs in mt7902\build as LocalSystem through the runner
# task (scripts\system-runner.ps1 -Install) and prints what it wrote.
#   run-system.ps1 <program> [args...]
param([Parameter(Mandatory, Position = 0)][string]$Program,
      [Parameter(ValueFromRemainingArguments)][string[]]$Rest,
      [int]$TimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'

$queue = Join-Path (Split-Path $PSScriptRoot -Parent) '.runner'
if (-not (Test-Path $queue)) { throw 'The runner is not installed.' }

$stem = Join-Path $queue ([guid]::NewGuid().ToString('N'))
@{ exe = $Program; args = @($Rest) } | ConvertTo-Json | Set-Content "$stem.tmp"
Rename-Item "$stem.tmp" "$stem.job"

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not (Test-Path "$stem.done")) {
  if ((Get-Date) -gt $deadline) { throw 'The runner did not answer: is its task running?' }
  Start-Sleep -Milliseconds 100
}
if (Test-Path "$stem.log") { Get-Content "$stem.log" }
"exit code $(Get-Content "$stem.done")"
Remove-Item "$stem.*" -ErrorAction SilentlyContinue
