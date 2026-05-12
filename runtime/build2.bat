@echo off
setlocal enabledelayedexpansion
cd /d "E:\Factorio-pt'\runtime"
echo Compiling with object files...

set GCC=C:\msys64\ucrt64\bin\g++.exe

echo [1/5] Compiling main.cpp...
"!GCC!" -c -std=c++20 -O3 -ffast-math -I. src\main.cpp -o main.o
if errorlevel 1 goto error

echo [2/5] Compiling world.cpp...
"!GCC!" -c -std=c++20 -O3 -ffast-math -I. src\world.cpp -o world.o
if errorlevel 1 goto error

echo [3/5] Compiling gameplay.cpp...
"!GCC!" -c -std=c++20 -O3 -ffast-math -I. src\gameplay.cpp -o gameplay.o
if errorlevel 1 goto error

echo [4/5] Compiling render.cpp...
"!GCC!" -c -std=c++20 -O3 -ffast-math -I. src\render.cpp -o render.o
if errorlevel 1 goto error

echo [5/5] Linking...
"!GCC!" -o factorio_pt_runtime.exe main.o world.o gameplay.o render.o -luser32 -lgdi32
if errorlevel 1 goto error

if exist factorio_pt_runtime.exe (
    echo.
    echo *** BUILD SUCCESS ***
    for %%F in (factorio_pt_runtime.exe) do echo Size: %%~zF bytes
    exit /b 0
) else (
    echo.
    echo Linking reported success but .exe not found
    exit /b 1
)

:error
echo.
echo Build failed!
exit /b 1
