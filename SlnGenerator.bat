@echo off
setlocal
REM shift also moves %0, so keep the script folder before parsing arguments.
set "ScriptDir=%~dp0"
set "NoPause="
REM Debugger setup waits on the symbol share, so it runs only when asked for.
set "WithDebugger="

:ParseArgs
if "%~1"=="" goto ArgsDone
if /I "%~1"=="--no-pause" set "NoPause=1"
if /I "%~1"=="--with-debugger" set "WithDebugger=1"
if /I "%~1"=="--no-debugger" set "WithDebugger="
shift
goto ParseArgs
:ArgsDone

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ScriptDir%Setup\Build\GenerateProjects.ps1"
set "GenerateExitCode=%ERRORLEVEL%"

REM Apply debugger settings without opening a Visual Studio window.
if not "%GenerateExitCode%"=="0" goto Finish
if not defined WithDebugger goto Finish
call "%ScriptDir%Setup\CrashDump\SetupKurisuDebugger.bat" --no-pause
set "GenerateExitCode=%ERRORLEVEL%"

:Finish
echo.
if not defined NoPause pause
exit /b %GenerateExitCode%
