param(
    [Parameter(Mandatory=$true)][string]$IdfRoot,
    [Parameter(Mandatory=$true)][string]$IdfToolsRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
$sdkSha=(& git -C $IdfRoot rev-parse HEAD).Trim()
if ($sdkSha -ne 'b774170ff46c393eeb5e495ea37936038d3f4f4f') { throw 'Pinned ESP-IDF 5.5.5 commit required.' }
$buildAbsolute=[IO.Path]::GetFullPath($OutputRoot)
if ($buildAbsolute -match 'prototype-approved-artifacts|link-approved-build') { throw 'Use a separate SDR-metal build directory.' }
$pythonScripts=Join-Path $IdfToolsRoot 'python_env/idf5.5_py3.12_env/Scripts'
$pythonExe=Join-Path $pythonScripts 'python.exe'
$env:IDF_TOOLS_PATH=[IO.Path]::GetFullPath($IdfToolsRoot)
$env:Path=$pythonScripts+';'+$env:Path
. (Join-Path $IdfRoot 'export.ps1')
$idfPy=Join-Path $IdfRoot 'tools/idf.py'
& $pythonExe $idfPy -C $PSScriptRoot -B $buildAbsolute -D IDF_TARGET=esp32 reconfigure build
if ($LASTEXITCODE -ne 0) { throw 'Isolated SDR-metal target build failed.' }
& $pythonExe (Join-Path $PSScriptRoot 'receipt.py') --build $buildAbsolute --sdk $IdfRoot
if ($LASTEXITCODE -ne 0) { throw 'Receipt generation failed.' }
Write-Output 'Built isolated SDR-metal evidence only; no COM, flash or RF operation requested.'
