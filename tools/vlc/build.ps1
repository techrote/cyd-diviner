[CmdletBinding()]
param(
    [string]$VlcPackageRoot,
    [string]$ToolchainRoot,
    [string]$OutputDirectory,
    [switch]$StagePortable
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$cache = Join-Path $repo '.pio\vlc-sdk'

function Expand-Dependency {
    param([string]$Url, [string]$Sha256, [string]$Folder, [string]$RequiredFile)

    $destination = Join-Path $cache $Folder
    if (Test-Path -LiteralPath (Join-Path $destination $RequiredFile) -PathType Leaf) {
        return $destination
    }

    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $archive = Join-Path $cache ($Url.Split('/')[-1])
    if (!(Test-Path -LiteralPath $archive -PathType Leaf) -or (Get-FileHash -LiteralPath $archive).Hash -ne $Sha256) {
        $download = "$archive.download"
        & curl.exe --fail --location --silent --show-error --output $download $Url
        if ($LASTEXITCODE -ne 0) { throw "Could not download $Url" }
        if ((Get-FileHash -LiteralPath $download).Hash -ne $Sha256) {
            throw "SHA-256 verification failed for $Url"
        }
        Move-Item -LiteralPath $download -Destination $archive -Force
    }

    & tar.exe -xf $archive -C $cache
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath (Join-Path $destination $RequiredFile) -PathType Leaf)) {
        throw "Could not extract $archive"
    }
    return $destination
}

function Get-PortableVlc {
    Expand-Dependency `
        'https://download.videolan.org/vlc/3.0.24/win64/vlc-3.0.24-win64.7z' `
        '1ed59c09152e78aff84663fa76efab26057716576db74332858810e5ef54ae1f' `
        'vlc-3.0.24' 'sdk\lib\libvlccore.lib'
}
if (!$VlcPackageRoot) {
    $VlcPackageRoot = Get-PortableVlc
}
if (!$ToolchainRoot) {
    $ToolchainRoot = Expand-Dependency `
        'https://github.com/mstorsjo/llvm-mingw/releases/download/20260602/llvm-mingw-20260602-msvcrt-x86_64.zip' `
        'a7d44091c4a362cfd13ad5f86541f8c91a85b96e4aae03c29995cda4a2214928' `
        'llvm-mingw-20260602-msvcrt-x86_64' 'bin\x86_64-w64-mingw32-clang.exe'
}
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repo '.pio\vlc-plugin'
}

$vlc = (Resolve-Path -LiteralPath $VlcPackageRoot).Path
$toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$output = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$sdk = Join-Path $vlc 'sdk'
$compiler = Join-Path $toolchain 'bin\x86_64-w64-mingw32-clang.exe'
$readObj = Join-Path $toolchain 'bin\llvm-readobj.exe'
$importLibrary = Join-Path $sdk 'lib\libvlccore.lib'
$metadata = Join-Path $sdk 'lib\pkgconfig\vlc-plugin.pc'
$plugin = Join-Path $output 'libcydpcm_plugin.dll'
$temporaryPlugin = "$plugin.tmp"

if (!(Select-String -LiteralPath $metadata -Pattern '^Version: 3\.0\.24$' -Quiet)) {
    throw 'The VideoLAN 3.0.24 Windows SDK is required.'
}

function Assert-X64Pe {
    param([string]$File)
    $headers = & $readObj --file-headers $File
    if ($LASTEXITCODE -ne 0 -or !($headers -match 'Machine: IMAGE_FILE_MACHINE_AMD64')) {
        throw "Expected an AMD64 PE file at $File"
    }
}

Assert-X64Pe (Join-Path $vlc 'libvlccore.dll')
$flags = @(
    '-std=gnu11', '-O2', '-shared', '-Wall', '-Wextra',
    '-D__PLUGIN__', '-D_FILE_OFFSET_BITS=64', '-D_REENTRANT', '-D_THREAD_SAFE',
    '-isystem', (Join-Path $sdk 'include'),
    '-isystem', (Join-Path $sdk 'include\vlc\plugins'),
    '-Wl,--no-undefined',
    (Join-Path $PSScriptRoot 'cydpcm.c'), $importLibrary,
    '-o', $temporaryPlugin
)
& $compiler @flags
if ($LASTEXITCODE -ne 0) { throw 'The CYD PCM plug-in build failed.' }

Assert-X64Pe $temporaryPlugin
$exports = & $readObj --coff-exports $temporaryPlugin
if ($LASTEXITCODE -ne 0 -or !($exports -match 'Name: vlc_entry__3_0_0f$')) {
    throw 'The compiled DLL is missing the VLC 3.0 plug-in entry point.'
}
$imports = & $readObj --coff-imports $temporaryPlugin
if ($LASTEXITCODE -ne 0 -or !($imports -match 'Name: libvlccore\.dll$')) {
    throw 'The compiled DLL is missing its import from libvlccore.dll.'
}

Move-Item -LiteralPath $temporaryPlugin -Destination $plugin -Force
if ($StagePortable) {
    $runtime = Get-PortableVlc
    Copy-Item -LiteralPath $plugin -Destination (Join-Path $runtime 'plugins\access\libcydpcm_plugin.dll') -Force
    Write-Host "Portable VLC: $(Join-Path $runtime 'vlc.exe')"
}
Get-Item -LiteralPath $plugin
