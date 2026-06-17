@echo off
setlocal
cd /d "%~dp0"
"%~dp0communitymp.exe" --client %*
exit /b %ERRORLEVEL%
