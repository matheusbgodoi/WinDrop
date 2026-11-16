# Development aid: runs the project's test programs as LocalSystem on request,
# so that each hardware test does not need its own elevation prompt.
#
#   system-runner.ps1 -Install    one elevation prompt; starts the runner task
#   system-runner.ps1 -Remove     one elevation prompt; stops and deletes it
#   system-runner.ps1 -Serve      what the task itself runs
#
# While it runs, anything in this user account can have the programs in
# mt7902\build run as LocalSystem by dropping a job file. It only ever starts
# executables from that folder, by bare name, and it ends by itself after
# twelve hours. Remove it when the session is over.
param([switch]$Install, [switch]$Remove, [switch]$Serve)
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent
$queue = Join-Path $repoRoot '.runner'
$buildDir = Join-Path $repoRoot 'mt7902\build'
$task = 'winawdl-system-runner'

function Invoke-Elevated([string]$Command) {
  Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList `
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $Command
}

if ($Install) {
  New-Item -ItemType Directory -Force $queue | Out-Null
  Remove-Item "$queue\*" -ErrorAction SilentlyContinue
  $run = "powershell -NoProfile -ExecutionPolicy Bypass -File \`"$PSCommandPath\`" -Serve"
  Invoke-Elevated "schtasks /create /tn $task /ru SYSTEM /sc once /st 00:00 /f /tr '$run'; schtasks /run /tn $task"
  "Runner task started. Remove it with: scripts\system-runner.ps1 -Remove"
  return
}

if ($Remove) {
  New-Item -ItemType File -Force "$queue\stop" | Out-Null
  Invoke-Elevated "schtasks /end /tn $task; schtasks /delete /tn $task /f"
  Remove-Item $queue -Recurse -Force -ErrorAction SilentlyContinue
  'Runner task removed.'
  return
}

if (-not $Serve) { throw 'Give -Install, -Remove or -Serve.' }

$until = (Get-Date).AddHours(12)
while ((Get-Date) -lt $until -and -not (Test-Path "$queue\stop")) {
  foreach ($jobFile in Get-ChildItem $queue -Filter '*.job' -ErrorAction SilentlyContinue) {
    $stem = Join-Path $queue $jobFile.BaseName
    $exit = -1
    try {
      $job = Get-Content $jobFile.FullName -Raw | ConvertFrom-Json
      if ($job.exe -notmatch '^[a-z0-9_]+$') { throw "refused program name '$($job.exe)'" }
      $exe = Join-Path $buildDir "$($job.exe).exe"
      if (-not (Test-Path $exe)) { throw "no such program: $exe" }
      $arguments = @($job.args | ForEach-Object { "$_" })
      & $exe @arguments *> "$stem.log"
      $exit = $LASTEXITCODE
    } catch {
      "runner: $_" >> "$stem.log"
    }
    Remove-Item $jobFile.FullName -Force
    "$exit" | Set-Content "$stem.done"
  }
  Start-Sleep -Milliseconds 100
}
