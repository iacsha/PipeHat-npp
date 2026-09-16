@echo off
REM Build and run PipeHat's standalone tests with MSVC.
REM
REM They are deliberately free of Windows headers so they also build with g++,
REM which is how they are run on a Linux box. This script is the Windows half:
REM it sets up the MSVC environment and runs each test, reporting its exit code.
REM
REM Usage: cmd /c tests\runtests.bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

echo === EndpointProfileTest ===
cl /nologo /std:c++17 /EHsc /W4 /I..\src EndpointProfileTest.cpp
if errorlevel 1 ( echo EPT_COMPILE=FAILED & exit /b 1 )
EndpointProfileTest.exe
echo EPT_EXIT=%errorlevel%

echo === FieldTreeTest ===
cl /nologo /std:c++17 /EHsc /W4 /I..\src FieldTreeTest.cpp
if errorlevel 1 ( echo FTT_COMPILE=FAILED & exit /b 1 )
FieldTreeTest.exe
echo FTT_EXIT=%errorlevel%
