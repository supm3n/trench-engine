@echo off
rem clang (llvm-mingw, primary compiler) for this shell. The second folder holds libc++ and the ASan runtime.
call "%~dp0env-common.cmd"
set "ANTANTA_LLVM=%ANTANTA_TC%\llvm-mingw-20260922\llvm-mingw-20260922-ucrt-x86_64"
set "PATH=%ANTANTA_LLVM%\bin;%ANTANTA_LLVM%\x86_64-w64-mingw32\bin;%PATH%"
