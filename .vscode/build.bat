@echo off
setlocal

set VSWHERE="C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo Could not find vswhere.exe - is Visual Studio installed?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set VSINSTALL=%%i
)

if not defined VSINSTALL (
    echo Could not find a Visual Studio install with the C++ build tools.
    exit /b 1
)

set VCVARS="%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo Could not find vcvars64.bat under %VSINSTALL%
    exit /b 1
)

set SRC=%1
if "%SRC%"=="" (
    echo Usage: build.bat ^<Server^|Client^>
    exit /b 1
)

set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call %VCVARS% >nul
if errorlevel 1 exit /b 1

if not exist "%~dp0build" mkdir "%~dp0build"

cl /nologo /EHsc /std:c++14 /W3 /Zi /FS /Fo:"%~dp0build\\" /Fd:"%~dp0build\%SRC%.pdb" "%~dp0..\%SRC%.cpp" /link ws2_32.lib /out:"%~dp0..\%SRC%.exe"
