#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = "E:\Factorio-pt'"
$BuildDir = "$ProjectRoot\build"
$RuntimeDir = "$BuildDir\runtime"

Write-Host "=== Factorio-pt Build ===" -ForegroundColor Cyan

# Setup
Write-Host "[1/5] Setup..." -ForegroundColor Yellow
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"

# Clean
Write-Host "[2/5] Clean..." -ForegroundColor Yellow
if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force -EA SilentlyContinue }
New-Item -ItemType Directory -Path $RuntimeDir -Force | Out-Null

# CMake
Write-Host "[3/5] CMake..." -ForegroundColor Yellow
Push-Location $BuildDir
& cmake -G "MinGW Makefiles" ".."
if ($LASTEXITCODE -ne 0) { Write-Host "CMAKE FAILED" -ForegroundColor Red; exit 1 }
Pop-Location

# Build
Write-Host "[4/5] Build..." -ForegroundColor Yellow
Push-Location $BuildDir
& cmake --build .
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; exit 1 }
Pop-Location

# DLLs - Runtime
Write-Host "[5/5] Runtime DLLs..." -ForegroundColor Yellow
Copy-Item "C:\msys64\ucrt64\bin\libstdc++-6.dll" "$RuntimeDir\" -Force -EA SilentlyContinue
Copy-Item "C:\msys64\ucrt64\bin\libwinpthread-1.dll" "$RuntimeDir\" -Force -EA SilentlyContinue
Copy-Item "C:\msys64\ucrt64\bin\libgcc_s_seh-1.dll" "$RuntimeDir\" -Force -EA SilentlyContinue

# Rust DLL
Write-Host "     Rust DLL..." -ForegroundColor Yellow
$RustDll = "$ProjectRoot\sim-rust\target\x86_64-pc-windows-gnu\debug\factorio_pt_sim.dll"
if (Test-Path $RustDll) {
  Copy-Item $RustDll "$RuntimeDir\" -Force -EA SilentlyContinue
  Write-Host "     ✓ Rust DLL copied" -ForegroundColor Green
} else {
  Write-Host "     ⚠ Rust DLL not found" -ForegroundColor Yellow
}

Write-Host "`n✓ DONE" -ForegroundColor Green
Write-Host "Run: .\build\runtime\factorio_pt_runtime.exe" -ForegroundColor Cyan


