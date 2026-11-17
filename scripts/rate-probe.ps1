# Tries data rates one by one: restarts AWDL at each and pings a peer's awdl0
# address with full-size packets. Loss and delay say whether the peer hears
# and acknowledges that rate, with nobody having to accept a transfer.
#   rate-probe.ps1 -Peer fe80::... -Rates 49,4c,87,108
param([Parameter(Mandatory)][string]$Peer, [string[]]$Rates = @('49', '4c'),
      [int]$Count = 40, [int]$Channel = 149)
$ErrorActionPreference = 'Stop'

foreach ($rate in $Rates) {
  & "$PSScriptRoot\mt7902-awdl.ps1" -Channel $Channel -Seconds 120 -DataRate $rate | Out-Null
  Start-Sleep -Seconds 10
  $index = (Get-NetAdapter -Name awdl0).ifIndex
  $reply = ping -6 -n $Count -l 1400 -w 1000 "$Peer%$index"
  $got = @($reply | Where-Object { $_ -match 'tempo[=<]|time[=<]' }).Count
  $times = $reply | ForEach-Object { if ($_ -match '(?:tempo|time)[=<](\d+)ms') { [int]$Matches[1] } }
  $median = if ($times) { ($times | Sort-Object)[[int]($times.Count / 2)] } else { '-' }
  "rate 0x${rate}: $got of $Count answered, median $median ms"
}
& "$PSScriptRoot\mt7902-awdl.ps1" -Stop | Out-Null
