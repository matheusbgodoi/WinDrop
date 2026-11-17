# Runs txtest while the Mac captures in monitor mode, then prints what the Mac
# heard from this card. Needs scripts\mac\install-sniffer.sh done on the Mac.
#   sniff-test.ps1 <channel> <cck|ofdm>[-wf0] [probes]
param([Parameter(Mandatory)][int]$Channel, [Parameter(Mandatory)][string]$Mode,
      [int]$Probes = 30, [string]$Mac = 'cc:47:40:2d:40:9a', [string]$MacHost = 'macbook')
$root = Split-Path $PSScriptRoot -Parent
$fw = Join-Path $root 'ref\mt76\firmware'
$seconds = [Math]::Min(60, 25 + [int]($Probes / 10))

$locked = ssh -o ConnectTimeout=10 $MacHost 'ioreg -n Root -d1 -a | grep -c CGSSessionScreenIsLocked'
if ($LASTEXITCODE -gt 1 -or $locked -ne '0') { throw 'The Mac is unreachable or its screen is locked: it would not rejoin Wi-Fi.' }
ssh $MacHost "nohup sudo -n /usr/local/winawdl/winawdl-sniff $Channel $seconds $Mac >/dev/null 2>&1 &"
Start-Sleep -Seconds 4
& "$PSScriptRoot\run-system.ps1" txtest "$fw\WIFI_MT7902_patch_mcu_1_1_hdr.bin" `
    "$fw\WIFI_RAM_CODE_MT7902_1.bin" $Channel $Probes $Mode | Select-Object -Last 3

Start-Sleep -Seconds ($seconds + 10)
$summary = 'f=/usr/local/winawdl/capture/last.pcap; ' +
    "echo `"from card: `$(tcpdump -r `$f -n 'wlan addr2 $Mac' 2>/dev/null | wc -l), " +
    "to card: `$(tcpdump -r `$f -n 'wlan addr1 $Mac' 2>/dev/null | wc -l)`"; " +
    "tcpdump -r `$f -n -e 'wlan addr2 $Mac' 2>/dev/null | head -3 | cut -c1-230; " +
    'cat /usr/local/winawdl/capture/last.log'
foreach ($try in 1..20) {
  $out = ssh -o ConnectTimeout=8 $MacHost $summary 2>$null
  if ($LASTEXITCODE -eq 0) { $out; return }
  Start-Sleep -Seconds 8
}
throw 'The Mac did not come back on the network.'
