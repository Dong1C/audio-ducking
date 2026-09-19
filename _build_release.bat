@echo off
setlocal
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >NUL 2>&1
cd /d "C:\Users\03203\Desktop\Projects\Audio Ducking\Audio_Ducking"
if exist out\build\x64-release\build.ninja goto build_step
echo === configure ===
cmake --preset x64-release
if errorlevel 1 exit /b 1
:build_step
echo === build ===
cmake --build --preset x64-release
echo === done exit=%errorlevel% ===