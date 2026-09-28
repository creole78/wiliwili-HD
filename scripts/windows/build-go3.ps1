param(
    [string]$MsysRoot = "C:\Users\1\projects\wiliwili-build-tools\msys64",
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$prefix = Join-Path $MsysRoot 'ucrt64'
$env:PATH = "$prefix\bin;$MsysRoot\usr\bin;" + $env:PATH
$build = Join-Path $repo 'build-go3'
& "$prefix\bin\cmake.exe" -S $repo -B $build -G Ninja `
    -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_CURL=ON `
    -DUSE_LIBROMFS=ON -DBRLS_UNITY_BUILD=OFF -DWIN32_TERMINAL=OFF `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DGO3_PLAYER_REGRESSION=OFF
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& "$prefix\bin\cmake.exe" --build $build -j $Jobs
if ($LASTEXITCODE) { throw 'Build failed' }
& "$prefix\bin\g++.exe" -std=c++17 "$repo\tests\go3_interaction_test.cpp" `
    -I "$repo\library\borealis\library\include" -I "$repo\wiliwili\include" `
    -o "$build\go3-interaction-test.exe"
if ($LASTEXITCODE) { throw 'Test compilation failed' }
& "$build\go3-interaction-test.exe"
if ($LASTEXITCODE) { throw 'Regression tests failed' }
& "$prefix\bin\python.exe" "$PSScriptRoot\package-go3.py" $repo $prefix
if ($LASTEXITCODE) { throw 'Packaging failed' }
