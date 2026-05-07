param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true
$rustTarget = "x86_64-pc-windows-gnu"
$cargoExe = Join-Path $HOME ".cargo\bin\cargo.exe"
$dotnetExe = "C:\Program Files\dotnet\dotnet.exe"

if (Test-Path "C:\msys64\ucrt64\bin") {
  $env:Path = "C:\msys64\ucrt64\bin;$env:Path"
}

Write-Host "==> Building C++ runtime ($Configuration)"
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
cmake -S . -B .\build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
if (Test-Path ".\build\compile_commands.json") {
  Copy-Item ".\build\compile_commands.json" ".\compile_commands.json" -Force
}
cmake --build .\build

Write-Host "==> Building Rust simulation"
$cargoArgs = @("build", "--manifest-path", ".\sim-rust\Cargo.toml", "--target", $rustTarget)
if ($Configuration -eq "Release") {
  $cargoArgs += "--release"
}
& $cargoExe @cargoArgs

Write-Host "==> Building C# tools"
& $dotnetExe build .\tools-csharp\FactorioPt.Tools.csproj -c $Configuration

Write-Host "Build completed."

