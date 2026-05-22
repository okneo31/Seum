@echo off
rem Seum build wrapper.
rem Auto: chcp 65001, MSVC env via vswhere, configure (once), incremental build.
chcp 65001 >nul

if defined VCINSTALLDIR goto have_msvc

set "_VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%_VSWHERE%" (
    echo [seum] vswhere.exe not found at standard path.
    echo        Install Visual Studio Build Tools, OR open
    echo        "x64 Native Tools Command Prompt for VS" and rerun.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%_VSWHERE%" -latest -products * -find VC\Auxiliary\Build\vcvars64.bat`) do (
    echo [seum] Activating MSVC env: %%i
    call "%%i" >nul
)
if not defined VCINSTALLDIR (
    echo [seum] Failed to activate MSVC env via vswhere.
    exit /b 1
)

:have_msvc

if not exist build\CMakeCache.txt (
    echo [seum] CMake configure...
    cmake -B build
    if errorlevel 1 goto error
)
echo [seum] Building...
cmake --build build --config Release
if errorlevel 1 goto error

echo.
echo [seum] OK. Next:
echo     test
echo     run examples\hello.seum
exit /b 0

:error
echo.
echo [seum] BUILD FAILED. See output above.
exit /b 1
