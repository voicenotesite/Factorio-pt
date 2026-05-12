#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
$env:CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER = "$env:USERPROFILE\.rustup\toolchains\stable-x86_64-pc-windows-gnu\lib\rustlib\x86_64-pc-windows-gnu\bin\rust-lld.exe"
$env:CARGO_TARGET_X86_64_PC_WINDOWS_GNU_RUSTFLAGS = "-C link-arg=-LC:/msys64/ucrt64/lib -C link-arg=-LC:/msys64/ucrt64/lib/gcc/x86_64-w64-mingw32/16.1.0"
Push-Location "E:\Factorio-pt'"
& "$env:USERPROFILE\.cargo\bin\rustup.exe" run stable-x86_64-pc-windows-gnu cargo run --manifest-path .\bevy-client\Cargo.toml --target x86_64-pc-windows-gnu
Pop-Location
