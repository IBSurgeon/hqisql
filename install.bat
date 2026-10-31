@echo off
::
:: install.bat [--check | --remove | --no-hook] <firebird-tree>
::
:: Installs hqisql into the Firebird source tree. Runs install.sh with the
:: bash of Git for Windows; see install.sh for the options.
::
setlocal
set "HQ_BASH="
if exist "%ProgramFiles%\Git\bin\bash.exe" set "HQ_BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined HQ_BASH for /f "delims=" %%g in ('where git.exe 2^>nul') do (
	if not defined HQ_BASH if exist "%%~dpg..\bin\bash.exe" set "HQ_BASH=%%~dpg..\bin\bash.exe"
)
if not defined HQ_BASH (
	echo install.bat: bash.exe of Git for Windows is not found
	exit /b 1
)
"%HQ_BASH%" "%~dp0install.sh" %*
exit /b %ERRORLEVEL%
