REM exception
@if "%~1" equ "" (
	@echo off
	call shared color c "This file should be run from f.e. 'build.release.64.bat' instead"
	pause
	exit /b %ERRORLEVEL%
)

REM build
call env
msbuild Dolphin_2010.sln %*
@if %ERRORLEVEL% neq 0 (
	pause
	exit /b %ERRORLEVEL%
)