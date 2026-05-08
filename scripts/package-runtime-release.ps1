param(
  [ValidateSet("Release")]
  [string]$Configuration = "Release",
  [string]$RustTarget = "x86_64-pc-windows-gnu",
  [string]$OutputDir = ".\dist"
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

$cargoExe = Join-Path $HOME ".cargo\bin\cargo.exe"
$buildDir = Join-Path $repoRoot "build"
$runtimeOutputDir = Join-Path $buildDir "runtime"
$packageDir = Join-Path $OutputDir "runtime-win64"
$archivePath = Join-Path $OutputDir "factorio-pt-runtime-win64.zip"

if (Test-Path "C:\msys64\ucrt64\bin") {
  $env:Path = "C:\msys64\ucrt64\bin;$env:Path"
}

Write-Host "==> Building runtime ($Configuration)"
if (Test-Path $buildDir) {
  Remove-Item -Recurse -Force $buildDir
}

cmake -S . -B $buildDir -G "MinGW Makefiles" "-DCMAKE_BUILD_TYPE=$Configuration" -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
cmake --build $buildDir

Write-Host "==> Building Rust simulation ($Configuration)"
$cargoArgs = @("build", "--manifest-path", ".\sim-rust\Cargo.toml", "--target", $RustTarget, "--release")
& $cargoExe @cargoArgs

Write-Host "==> Preparing runtime package"
if (Test-Path $packageDir) {
  Remove-Item -Recurse -Force $packageDir
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

$runtimeExe = Join-Path $runtimeOutputDir "factorio_pt_runtime.exe"
$simDll = Join-Path $repoRoot "sim-rust\target\$RustTarget\release\factorio_pt_sim.dll"

if (!(Test-Path $runtimeExe)) {
  throw "Missing runtime executable: $runtimeExe"
}
if (!(Test-Path $simDll)) {
  throw "Missing Rust simulation DLL: $simDll"
}

Copy-Item $runtimeExe $packageDir -Force
Copy-Item $simDll $packageDir -Force

$runtimeDeps = @(
  "C:\msys64\ucrt64\bin\libstdc++-6.dll",
  "C:\msys64\ucrt64\bin\libwinpthread-1.dll",
  "C:\msys64\ucrt64\bin\libgcc_s_seh-1.dll"
)

foreach ($dep in $runtimeDeps) {
  if (Test-Path $dep) {
    Copy-Item $dep $packageDir -Force
  } else {
    throw "Missing runtime dependency: $dep"
  }
}

if (!(Test-Path $OutputDir)) {
  New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}
if (Test-Path $archivePath) {
  Remove-Item $archivePath -Force
}

Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $archivePath -Force
Write-Host "Runtime package ready: $archivePath"
