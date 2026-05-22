@echo off
rem Seum run wrapper. chcp 65001 + seum.exe with arguments.
rem ASCII-only body (Korean in .bat breaks cmd parser on CP949 systems).
chcp 65001 >nul

if "%~1"=="" (
    echo Usage: run ^<file^>
    echo Example: run examples\hello.seum
    exit /b 2
)

if not exist build\Release\seum.exe (
    echo [seum] seum.exe missing. Building first...
    call "%~dp0build.bat"
    if errorlevel 1 exit /b 1
)

build\Release\seum.exe %*
exit /b %errorlevel%
