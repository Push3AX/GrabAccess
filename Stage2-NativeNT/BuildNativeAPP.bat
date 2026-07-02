@echo off
setlocal
cd /d "%~dp0\.."
call make_all.bat --stage2-only
exit /b %ERRORLEVEL%
