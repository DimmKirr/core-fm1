# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/fmlab.c on Windows with MSVC (tests/run_host.sh is the cc/sh version).
#   pwsh tests\run_host.ps1 [script.txt]
# Out: build\host\fmlab.wav and build\host\fmlab-<ms>.ppm
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
New-Item -ItemType Directory -Force -Path build\host | Out-Null

$vcvars = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat' |
          Select-Object -First 1 -ExpandProperty FullName
if (-not $vcvars) { throw 'vcvars64.bat not found: install the Visual Studio Build Tools (C++ workload).' }

$ver = if ($env:APP_VER) { $env:APP_VER } else { 'dev' }
# msvc.h is force-included: cl is not gcc, and the host half of platform.c still carries one gcc attribute.
$cl = "cl /nologo /TC /O2 /W3 /DDINGHY_HOST /DAPP_VER=\""$ver\"" /Itests /FImsvc.h " +
      "/Ifirmware\src /Ifirmware\gen firmware\src\fmlab.c /Fo:build\host\ /Fe:build\host\fmlab.exe"
& $env:ComSpec /c "call `"$vcvars`" >nul && $cl"
if ($LASTEXITCODE) { throw 'build failed' }

& build\host\fmlab.exe build\host\fmlab @args
if ($LASTEXITCODE) { throw 'run failed' }
