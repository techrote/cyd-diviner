param(
    [Parameter(Mandatory=$true)][string]$IdfRoot,
    [Parameter(Mandatory=$true)][string]$IdfToolsRoot,
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
$experimentRoot = $PSScriptRoot
$sdkSha = (& git -C $IdfRoot rev-parse HEAD).Trim()
if ($sdkSha -ne 'b774170ff46c393eeb5e495ea37936038d3f4f4f') {
    throw 'This experimental RAM ABI is pinned to ESP-IDF b774170 (5.5.5).'
}
$pythonEnv = Join-Path $IdfToolsRoot 'python_env/idf5.5_py3.12_env/Scripts'
$pythonExe = Join-Path $pythonEnv 'python.exe'
if (!(Test-Path -LiteralPath $pythonExe)) { throw 'Install the pinned SDK Python environment first.' }
$env:IDF_TOOLS_PATH = (Resolve-Path -LiteralPath $IdfToolsRoot).Path
$env:Path = $pythonEnv + ';' + $env:Path
. (Join-Path $IdfRoot 'export.ps1')
$idfPy = Join-Path $IdfRoot 'tools/idf.py'
$rxRoot = Join-Path $OutputRoot 'rx-source'
& $pythonExe (Join-Path $experimentRoot 'prepare_rx.py') --upstream $UpstreamRoot --output $rxRoot
if ($LASTEXITCODE -ne 0) { throw 'Receiver source preparation failed.' }
$rxArgs = @('-C', $rxRoot, '-B', (Join-Path $OutputRoot 'rx-build'), '-D', 'IDF_TARGET=esp32s3', 'build')
& $pythonExe $idfPy @rxArgs
if ($LASTEXITCODE -ne 0) { throw 'Receiver build failed.' }
$txArgs = @('-C', (Join-Path $experimentRoot 'tx'), '-B', (Join-Path $OutputRoot 'tx-build'), '-D', 'IDF_TARGET=esp32', 'build')
& $pythonExe $idfPy @txArgs
if ($LASTEXITCODE -ne 0) { throw 'Transmitter build failed.' }
Write-Output 'Built standalone experiments only. No serial port, flash operation or RF transmission was requested.'
