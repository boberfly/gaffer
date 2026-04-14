@echo off

rem Public-facing launch script for gaffer. This sets up the Python interpreter
rem and then defers to `_gaffer.py` to set up the appropriate environment
rem and finally launch `__gaffer.py`.

setlocal EnableDelayedExpansion

set "HOME=%USERPROFILE:\=/%"

set GAFFER_ROOT=%~dp0%..
set PATH=%GAFFER_ROOT%\bin;%PATH%

if "%GAFFER_DEBUG%" NEQ "" (
	%GAFFER_DEBUGGER% "%GAFFER_ROOT%"\bin\__private\gaffer.exe "%GAFFER_ROOT%"/bin/__private/_gaffer.py %*
) else (
	"%GAFFER_ROOT%"\bin\__private\gaffer.exe "%GAFFER_ROOT%"/bin/__private/_gaffer.py %*
)

endlocal
exit /B %ERRORLEVEL%
