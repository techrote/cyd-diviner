[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]{0,4}$')]
    [string]$Port = 'COM21',

    [ValidateRange(50, 2000)]
    [int]$CachingMs = 150,

    [string]$VlcPath = (Join-Path $env:ProgramFiles 'VideoLAN\VLC\vlc.exe'),

    [string]$PluginDirectory = (Join-Path $PSScriptRoot '..\..\.pio\vlc-plugin')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $VlcPath -PathType Leaf)) {
    throw 'VLC was not found. Pass the path to VLC 3.0.24 (Windows x64) with -VlcPath.'
}
if (-not (Test-Path -LiteralPath (Join-Path $PluginDirectory 'libcydpcm_plugin.dll') -PathType Leaf)) {
    throw 'The CYD PCM plugin is missing. Build it with tools/vlc/build.ps1 first.'
}

$VlcPath = (Resolve-Path -LiteralPath $VlcPath).ProviderPath
$originalPluginPath = $env:VLC_PLUGIN_PATH
try {
    $env:VLC_PLUGIN_PATH = (Resolve-Path -LiteralPath $PluginDirectory).ProviderPath
    Start-Process -FilePath $VlcPath -WindowStyle Normal -ArgumentList @(
        '--ignore-config', '--no-one-instance', '--no-plugins-cache', "--live-caching=$CachingMs", "cydpcm://$Port"
    )
} finally {
    $env:VLC_PLUGIN_PATH = $originalPluginPath
}
