param(
    [Parameter(Mandatory=$true)][string]$IdfRoot,
    [Parameter(Mandatory=$true)][string]$IdfToolsRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
$sdkSha = (& git -C $IdfRoot rev-parse HEAD).Trim()
if ($sdkSha -ne 'b774170ff46c393eeb5e495ea37936038d3f4f4f') {
    throw 'Snapshot PHY ABI requires ESP-IDF b774170 (5.5.5).'
}
$pythonDirectory = Join-Path $IdfToolsRoot 'python_env/idf5.5_py3.12_env/Scripts'
$pythonExe = Join-Path $pythonDirectory 'python.exe'
$env:IDF_TOOLS_PATH = (Resolve-Path -LiteralPath $IdfToolsRoot).Path
$env:Path = $pythonDirectory + ';' + $env:Path
. (Join-Path $IdfRoot 'export.ps1')
& $pythonExe (Join-Path $IdfRoot 'tools/idf.py') -C $PSScriptRoot -B $BuildDirectory -D IDF_TARGET=esp32s3 build
if ($LASTEXITCODE -ne 0) { throw 'Minimal snapshot build failed.' }
Write-Output 'Built isolated S3 snapshot probe. No serial access, flashing or RF capture requested.'
