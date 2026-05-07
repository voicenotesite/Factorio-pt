param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

Write-Host "==> Building C++ runtime ($Configuration)"
cmake -S . -B .\build -DCMAKE_BUILD_TYPE=$Configuration
cmake --build .\build --config $Configuration

Write-Host "==> Building Rust simulation"
push-location .\sim-rust
cargo build
pop-location

Write-Host "==> Building C# tools"
dotnet build .\tools-csharp\FactorioPt.Tools.csproj -c $Configuration

Write-Host "Build completed."

