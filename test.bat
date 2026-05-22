@echo off
rem Seum test wrapper. Auto build + ctest.
chcp 65001 >nul

call "%~dp0build.bat"
if errorlevel 1 exit /b 1

echo.
echo [seum] Running tests...
ctest --test-dir build -C Release --output-on-failure
exit /b %errorlevel%
