@echo off
REM Build UnusedBattleCursorRestore.dll

set "DESTINATION_DIR=C:\Users\Pseudonym_Tim\Desktop\Tools\Mewtator\mods\UnusedBattleCursorRestore"
set "MEWTATOR_DEPLOY=true"

setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
    set "VSDIR=%%i"
)

if not defined VSDIR (
    echo ERROR: Could not find a Visual Studio installation.
    pause
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

echo Building UnusedBattleCursorRestore.dll...
cl /LD /O2 /GS- /W3 /D_CRT_SECURE_NO_WARNINGS /TC src\UnusedBattleCursorRestore.c /Fe:UnusedBattleCursorRestore.dll /link user32.lib kernel32.lib

if %ERRORLEVEL% NEQ 0 (
    echo Build FAILED.
    pause
    exit /b 1
)

del /Q UnusedBattleCursorRestore.obj UnusedBattleCursorRestore.lib UnusedBattleCursorRestore.exp 2>nul

if /I "%MEWTATOR_DEPLOY%"=="true" (
    set "DEPLOY_DIR=%DESTINATION_DIR%"
) else (
    set "DEPLOY_DIR=%DESTINATION_DIR%\mods"
)

if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
copy /Y UnusedBattleCursorRestore.dll "%DEPLOY_DIR%\UnusedBattleCursorRestore.dll"
copy /Y description.json "%DEPLOY_DIR%\description.json"
copy /Y preview.png "%DEPLOY_DIR%\preview.png"

echo Deployed to %DEPLOY_DIR%