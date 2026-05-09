# Orbitum — Generate textures with Stable Diffusion (local, no account needed)
# by Sh1t3ad Dev
#
# First run: installs PyTorch + diffusers (~3 GB pip), then downloads SDXL-Turbo (~7 GB).
# Subsequent runs: instant start (cached).
#
# Requirements: Python 3.10+ and CUDA drivers (RTX 3060 — CUDA 12.x)
#
# Usage:
#   .\scripts\generate_sd_textures.ps1
#   .\scripts\generate_sd_textures.ps1 -Model sd-2.1 -Seed 1337

param(
    [string]$Model  = "sdxl-turbo",
    [int]   $Seed   = 42,
    [string]$Output = "assets/generated/sd_tiles"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot\..

Write-Host "==> Orbitum — SD Texture Generator" -ForegroundColor Cyan
Write-Host "    Model : $Model"
Write-Host "    Seed  : $Seed"
Write-Host "    Output: $Output"

# ── Check Python ─────────────────────────────────────────────────────────────
$python = $null
foreach ($cmd in @("python", "python3", "py")) {
    try {
        $ver = & $cmd --version 2>&1
        if ($ver -match "3\.\d+") { $python = $cmd; break }
    } catch {}
}
if (-not $python) {
    Write-Error "Python 3.10+ not found. Download from https://www.python.org/downloads/"
    exit 1
}
Write-Host "==> Python: $python ($(&$python --version))" -ForegroundColor Green

# ── Install dependencies ──────────────────────────────────────────────────────
Write-Host "==> Installing PyTorch + diffusers (skip if already installed)..." -ForegroundColor Yellow
# PyTorch with CUDA 12.1 — adjust cu121 if you have a different CUDA version
& $python -m pip install --quiet torch torchvision --index-url https://download.pytorch.org/whl/cu121
& $python -m pip install --quiet -r tools-python/requirements.txt

# ── Generate textures ─────────────────────────────────────────────────────────
Write-Host "==> Generating textures with Stable Diffusion..." -ForegroundColor Yellow
& $python tools-python/sd_texture_gen.py --model $Model --seed $Seed --output-dir $Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# ── Repackage atlas ───────────────────────────────────────────────────────────
Write-Host "==> Repackaging texture atlas..." -ForegroundColor Yellow
dotnet run --project tools-csharp/ai-trainer -- --variants 16 --output assets/generated/runtime_texture_atlas.bin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item assets\generated\runtime_texture_atlas.bin build\runtime\ -Force
Write-Host "==> Done! Launch the game to see SD textures:" -ForegroundColor Green
Write-Host "    .\build\runtime\factorio_pt_runtime.exe"
