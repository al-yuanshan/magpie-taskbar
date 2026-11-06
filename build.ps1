param([ValidateSet('x64', 'Win32', 'ARM64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$taskCmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $taskCmake) {
    throw 'Install Visual Studio 2022 Desktop development with C++ and CMake tools, then run from Developer PowerShell.'
}
& cmake -S . -B "build/$Architecture" -G 'Visual Studio 17 2022' -A $Architecture
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build "build/$Architecture" --config Release
if ($LASTEXITCODE) { throw 'Build failed' }
& ctest --test-dir "build/$Architecture" -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
& cmake --install "build/$Architecture" --config Release --prefix "dist/$Architecture"
if ($LASTEXITCODE) { throw 'Install failed' }
Write-Host "DLL: $PSScriptRoot/dist/$Architecture/magpie-taskbar.dll"
