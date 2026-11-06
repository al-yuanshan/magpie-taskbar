param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][ValidateSet('x64', 'Win32')][string]$Architecture,
    [string]$DllPath = "dist/$Architecture/magpie-taskbar.dll",
    [string]$OutputDirectory = 'dist/release'
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not (Test-Path -LiteralPath $DllPath)) { throw "DLL not found: $DllPath" }
$name = "magpie-taskbar-v$Version-$Architecture"
$stage = Join-Path $OutputDirectory $name
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $DllPath -Destination $stage
Copy-Item -LiteralPath 'packaging/INSTALL.md' -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath 'LICENSE', 'THIRD_PARTY_NOTICES.md' -Destination $stage
$zip = Join-Path $OutputDirectory "$name.zip"
Compress-Archive -Path "$stage/*" -DestinationPath $zip -Force
Write-Host "Packaged $zip"
