param(
    [string]$OutDir = "",
    [string]$ObjDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = Join-Path $Root "build\cli" }
if (-not $ObjDir) { $ObjDir = Join-Path $Root "build\cli-obj" }

$MsysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$MsysCandidates = @(
    (Join-Path $MsysRoot "ucrt64\bin"),
    (Join-Path $MsysRoot "mingw64\bin")
)
$MsysBin = $MsysCandidates | Where-Object {
    (Test-Path (Join-Path $_ "gcc.exe")) -and (Test-Path (Join-Path $_ "g++.exe"))
} | Select-Object -First 1
if (-not $MsysBin) {
    throw "MSYS2 gcc/g++ not found under $MsysRoot\ucrt64\bin or $MsysRoot\mingw64\bin"
}
$Gcc = Join-Path $MsysBin "gcc.exe"
$Gxx = Join-Path $MsysBin "g++.exe"

$CubiomesRoot = Join-Path $Root "cubiomes"
if (-not (Test-Path (Join-Path $CubiomesRoot "finders.c"))) {
    throw "cubiomes submodule not found at $CubiomesRoot. Run 'git submodule update --init --recursive' in FortressFinderBase."
}

New-Item -ItemType Directory -Force -Path $OutDir, $ObjDir | Out-Null
$env:PATH = "$MsysBin;$(Join-Path $MsysRoot 'usr\bin');$env:PATH"

# The sources include "cubiomes/...", so the include root is FortressFinderBase.
$Includes = @(
    "-I$Root"
)

$CSources = @()
$CSources += Get-ChildItem -Path $CubiomesRoot -Filter *.c -File |
    Where-Object { $_.Name -ne "tests.c" } |
    ForEach-Object { @{ Path = $_.FullName; Name = $_.BaseName } }
$FeaturesRoot = Join-Path $CubiomesRoot "features"
if (Test-Path $FeaturesRoot) {
    $CSources += Get-ChildItem -Path $FeaturesRoot -Filter *.c -File |
        ForEach-Object { @{ Path = $_.FullName; Name = "feature_$($_.BaseName)" } }
}

$CppSources = @(
    @{ Path = (Join-Path $Root "fortress_finder.cpp"); Name = "fortress_finder" },
    @{ Path = (Join-Path $Root "fortress_cli.cpp"); Name = "fortress_cli" }
)

$Objs = @()
foreach ($src in $CSources) {
    $obj = Join-Path $ObjDir "$($src.Name).o"
    Write-Host "CC  $($src.Path)"
    & $Gcc -O3 -DNDEBUG -D_WIN32 -std=c11 -ffast-math @Includes -c $src.Path -o $obj
    if ($LASTEXITCODE -ne 0) { throw "gcc failed on $($src.Path)" }
    $Objs += $obj
}

foreach ($src in $CppSources) {
    $obj = Join-Path $ObjDir "$($src.Name).o"
    Write-Host "CXX $($src.Path)"
    & $Gxx -O3 -DNDEBUG -D_WIN32 -std=c++17 -ffast-math @Includes -c $src.Path -o $obj
    if ($LASTEXITCODE -ne 0) { throw "g++ failed on $($src.Path)" }
    $Objs += $obj
}

$Exe = Join-Path $OutDir "fortressfinder-cli.exe"
Write-Host "LINK $Exe"
& $Gxx -static -o $Exe @Objs -static-libgcc -static-libstdc++
if ($LASTEXITCODE -ne 0) { throw "link failed" }
Write-Host "Built $Exe"
Get-Item $Exe | Format-List FullName, Length, LastWriteTime
