# Compiles the MT7902 user-space tools into mt7902\build.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\scripts\toolchain.ps1"

$tools = Initialize-Toolchain
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null

$common = @("$PSScriptRoot\mt7902_hw.c", "$PSScriptRoot\dma_mem.c",
            "$PSScriptRoot\..\tools\regproxy_client.c")
foreach ($tool in 'fwboot') {
  & $tools.Cl /nologo /W4 /WX /O2 /Zi "/Fo$out\\" "/Fd$out\$tool.pdb" `
      "/Fe$out\$tool.exe" "$PSScriptRoot\$tool.c" @common setupapi.lib advapi32.lib
  if ($LASTEXITCODE -ne 0) { throw "$tool failed to compile ($LASTEXITCODE)" }
}
Get-ChildItem $out -Filter *.exe | Select-Object Name, Length
