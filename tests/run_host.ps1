# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/fmlab.c on Windows with MSVC (tests/run_host.sh is the cc/sh version).
#   pwsh tests\run_host.ps1 [script.txt]
# Out: build\host\<script>.wav and build\host\<script>-<ms>.ppm ("fmlab" with no script).
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

# a script's output is named after it, so the runs do not overwrite each other
$out = 'build\host\fmlab'
if ($args.Count) { $out = Join-Path 'build\host' ([IO.Path]::GetFileNameWithoutExtension($args[0])) }
Remove-Item "$out-*.ppm" -ErrorAction SilentlyContinue
# the bank file outlives a run, the way the flash sector does: clear it so a run starts from nothing
Remove-Item "$out-bank.bin", "$out-out.syx" -ErrorAction SilentlyContinue

if ($args.Count -and [IO.Path]::GetFileName($args[0]) -eq 'bank.txt') {
    python tests\mksyx.py build\host          # the .syx files the script sends itself
    if ($LASTEXITCODE) { throw 'could not make the test .syx files' }
}

& build\host\fmlab.exe $out @args
if ($LASTEXITCODE) { throw 'run failed' }

if ($args.Count -and [IO.Path]::GetFileName($args[0]) -eq 'operators.txt') {
    python tests\wavcheck.py "$out.wav"      # the switches have to agree with the sound
    if ($LASTEXITCODE) { throw 'the sound does not match the operator switches' }
}
if ($args.Count -and [IO.Path]::GetFileName($args[0]) -eq 'bank.txt') {
    python tests\syxcheck.py $out            # and what was stored with what was sent
    if ($LASTEXITCODE) { throw 'the bank does not match what was sent to it' }
}
