@echo off
::
::  The contents of this file are subject to the Initial
::  Developer's Public License Version 1.0 (the "License");
::  you may not use this file except in compliance with the
::  License. You may obtain a copy of the License at
::  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
::
::  Software distributed under the License is distributed AS IS,
::  WITHOUT WARRANTY OF ANY KIND, either express or implied.
::  See the License for the specific language governing rights
::  and limitations under the License.
::
::  The Original Code was created by IBSurgeon
::  for the Firebird Open Source RDBMS project.
::
::  Copyright (c) 2026 IBSurgeon
::  and all contributors signed below.
::
::  All Rights Reserved.
::  Contributor(s): ______________________________________.
::==============
:: make_hqisql.bat [DEBUG] [CLEAN]
::
::   Builds hqisql (isql with the replxx line editor) and its unit tests
::   with a separate solution, HQisql.sln. The regular Firebird.sln does not
::   know about hqisql, so make_all.bat output is not affected.
::
::   Run after make_all.bat: make_all.bat cleans the output directory.

set ERRLEV=0

:: Set env vars
@call setenvvar.bat %*

@if errorlevel 1 (call :ERROR Executing setenvvar.bat failed & goto :EOF)

:: verify that make_all was run before
@if not exist %FB_OUTPUT_DIR%\fbclient.dll (goto :HELP_ALL & goto :EOF)


::==========
:: MAIN

@echo Building hqisql %FB_OBJ_DIR%

call compile.bat builds\win32\%VS_VER%\HQisql make_hqisql_%FB_TARGET_PLATFORM%.log
if errorlevel 1 (call :ERROR build failed - see make_hqisql_%FB_TARGET_PLATFORM%.log for details & goto :EOF)

@echo Running hqisql_test
@%FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\hqisql_test\hqisql_test.exe
if errorlevel 1 (call :ERROR hqisql_test failed & goto :EOF)

:: The kit gets hqisql.exe and the replxx license explicitly: make_all.bat
:: copies temp\...\firebird\* and does not know about hqisql.
@copy %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\hqisql\hqisql.exe %FB_OUTPUT_DIR% >nul
if errorlevel 1 (call :ERROR copy of hqisql.exe failed & goto :EOF)
@copy %FB_ROOT_PATH%\extern\replxx\LICENSE.md %FB_OUTPUT_DIR%\replxx-LICENSE.md >nul
if errorlevel 1 (call :ERROR copy of the replxx license failed & goto :EOF)
@echo hqisql.exe and replxx-LICENSE.md copied to %FB_OUTPUT_DIR%

@goto :EOF

::==============
:HELP_ALL
@echo.
@echo    You must run make_all.bat before running this script
@echo.
@goto :EOF

:ERROR
::====
@echo.
@echo   An error occurred while running make_hqisql.bat -
@echo     %*
@echo.
set ERRLEV=1
cancel_script > nul 2>&1
::End of ERROR
::------------
@goto :EOF
