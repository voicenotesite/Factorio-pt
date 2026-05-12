@echo off
setlocal enabledelayedexpansion
cd /d "E:\Factorio-pt'\runtime"
echo Compiling Orbitum runtime...
echo.

set GCC_PATH=C:\msys64\ucrt64\bin\g++.exe
if not exist "!GCC_PATH!" (
    echo Error: g++ not found at !GCC_PATH!
    exit /b 1
)

echo Using compiler: !GCC_PATH!
echo.

"!GCC_PATH!" -std=c++20 -O3 -ffast-math -Wall -v ^
  -I. ^
  src\main.cpp src\world.cpp src\gameplay.cpp src\render.cpp ^
  -o factorio_pt_runtime.exe ^
  -luser32 -lgdi32

echo.
echo Checking result...
if exist factorio_pt_runtime.exe (
    echo *** BUILD SUCCESS ***
    for %%F in (factorio_pt_runtime.exe) do echo Size: %%~zF bytes
) else (
    echo Build failed!
    exit /b 1
)
