@echo off
rem Puts the pinned cmake and ninja first on PATH (toolchains.txt; fetch-toolchains.ps1 installs them).
rem check.ps1 sets ANTANTA_TC when it runs from a snapshot of the sources (no toolchains folder beside it).
if not defined ANTANTA_TC set "ANTANTA_TC=%~dp0..\..\toolchains"
set "PATH=%ANTANTA_TC%\cmake-4.4.4\cmake-4.4.4-windows-x86_64\bin;%ANTANTA_TC%\ninja-1.13.2;%PATH%"
