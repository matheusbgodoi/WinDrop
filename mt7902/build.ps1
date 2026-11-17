# Compiles the MT7902 user-space tools into mt7902\build.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\scripts\toolchain.ps1"

$tools = Initialize-Toolchain
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null

$common = @("$PSScriptRoot\mt7902_hw.c", "$PSScriptRoot\mt7902_radio.c",
            "$PSScriptRoot\dma_mem.c", "$PSScriptRoot\..\tools\regproxy_client.c")
foreach ($tool in 'fwboot', 'rxtest', 'txtest', 'radiod') {
  $log = & $tools.Cl /nologo /W4 /WX /O2 /Zi "/Fo$out\\" "/Fd$out\$tool.pdb" `
      "/Fe$out\$tool.exe" "$PSScriptRoot\$tool.c" @common setupapi.lib advapi32.lib
  if ($LASTEXITCODE -ne 0) {
    $log | Where-Object { $_ -match 'error|warning' }
    throw "$tool failed to compile ($LASTEXITCODE)"
  }
}
# Programs that do not touch the card.
foreach ($tool in 'detach', 'radioclient') {
  $log = & $tools.Cl /nologo /W4 /WX /O2 "/Fo$out\\" "/Fe$out\$tool.exe" "$PSScriptRoot\$tool.c"
  if ($LASTEXITCODE -ne 0) {
    $log | Where-Object { $_ -match 'error|warning' }
    throw "$tool failed to compile ($LASTEXITCODE)"
  }
}
Get-ChildItem $out -Filter *.exe | Select-Object Name, Length
