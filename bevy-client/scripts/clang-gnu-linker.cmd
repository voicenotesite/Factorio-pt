@echo off
setlocal
"C:\Program Files\LLVM\bin\clang++.exe" --target=x86_64-w64-windows-gnu --sysroot=C:\msys64\ucrt64 -fuse-ld=lld -LC:/msys64/ucrt64/lib -LC:/msys64/ucrt64/lib/gcc/x86_64-w64-mingw32/16.1.0 %*
exit /b %ERRORLEVEL%
