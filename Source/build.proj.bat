REM exception
@if "%~1" equ "" (
	@echo off
	call shared color c "This file should be run from 'build.all.bat' or 'clean.bat' instead"
	pause
	exit /b %ERRORLEVEL%
)

REM build
call env
msbuild msbuild.proj %*
@if %ERRORLEVEL% neq 0 (
	pause
	exit /b %ERRORLEVEL%
)