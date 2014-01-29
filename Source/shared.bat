REM shared code

REM exception
@if "%~1" equ "" (
	@echo off
	call shared color c "This file should be run from another file"
	pause
	exit /b %ERRORLEVEL%
)

REM call function
call :%*
exit /b

REM color text
:color
@echo off
setlocal
pushd %temp%
for /F "tokens=1 delims=#" %%a in ('"prompt #$H#$E# & echo on & for %%b in (1) do rem"') do (
	<nul set/p"=%%a" >"%~2"
)
findstr /v /a:%1 /R "^$" "%~2" nul
del "%~2" > nul 2>&1
popd
@echo on