@echo off
setlocal
cd /d "%~dp0"
"%~dp0communitymp.exe" --server %*
set "exitCode=%ERRORLEVEL%"
if not "%exitCode%"=="0" (
  echo.
  echo CommunityMP dedicated server exited with code %exitCode%.
  pause
)
exit /b %exitCode%
