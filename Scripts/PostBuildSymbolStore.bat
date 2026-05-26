@echo off
setlocal EnableDelayedExpansion

rem ============================================================
rem PostBuildSymbolStore.bat
rem
rem Usage:
rem   PostBuildSymbolStore.bat <OutDir> <ConfigPlatform> <ProjectDir>
rem
rem Required env vars (set per machine to enable upload):
rem   ENABLE_SYMBOL_UPLOAD = 1
rem   SYMBOL_STORE_PATH    = path to symbol store (e.g. \\server\symbols)
rem
rem Optional env var:
rem   GIT_COMMIT           = override git commit hash
rem
rem Flow:
rem   0. Check ENABLE_SYMBOL_UPLOAD
rem   1. srctool.exe  - extract source file list from PDB
rem   2. pdbstr.exe   - write srcsrv stream into PDB
rem   3. symstore.exe - register PDB in Symbol Store
rem ============================================================

set "OUT_DIR=%~1"
set "CONFIG=%~2"
set "PROJECT_DIR=%~3"

set "PRODUCT=JungleEngine"

set "SRCSRV_DIR=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\srcsrv"
set "SRCTOOL=%SRCSRV_DIR%\srctool.exe"
set "PDBSTR=%SRCSRV_DIR%\pdbstr.exe"
set "SYMSTORE=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symstore.exe"

rem ------------------------------------------------------------
rem Upload opt-in guard
rem ------------------------------------------------------------

if not "%ENABLE_SYMBOL_UPLOAD%"=="1" (
    echo [SymbolStore] Symbol upload disabled. Set ENABLE_SYMBOL_UPLOAD=1 to enable.
    exit /b 0
)

rem ------------------------------------------------------------
rem Validate arguments
rem ------------------------------------------------------------

if "%OUT_DIR%"=="" (
    echo [SymbolStore] Usage: PostBuildSymbolStore.bat ^<OutDir^> ^<ConfigPlatform^> ^<ProjectDir^>
    exit /b 1
)

if "%CONFIG%"=="" (
    set "CONFIG=UnknownConfig"
)

if not exist "%OUT_DIR%" (
    echo [SymbolStore] OutDir not found: "%OUT_DIR%"
    exit /b 0
)

rem ------------------------------------------------------------
rem Validate tools
rem ------------------------------------------------------------

if not exist "%SRCTOOL%" (
    echo [SymbolStore] srctool.exe not found: "%SRCTOOL%"
    exit /b 1
)

if not exist "%PDBSTR%" (
    echo [SymbolStore] pdbstr.exe not found: "%PDBSTR%"
    exit /b 1
)

if not exist "%SYMSTORE%" (
    echo [SymbolStore] symstore.exe not found: "%SYMSTORE%"
    exit /b 1
)

rem ------------------------------------------------------------
rem Validate Symbol Store path
rem ------------------------------------------------------------

if "%SYMBOL_STORE_PATH%"=="" (
    echo [SymbolStore] SYMBOL_STORE_PATH is not set. Skipping upload.
    exit /b 0
)

rem ------------------------------------------------------------
rem Auto-detect Git source root
rem ------------------------------------------------------------

set "SOURCE_ROOT="

if not "%PROJECT_DIR%"=="" (
    for /f "usebackq tokens=*" %%R in (`git -C "%PROJECT_DIR%" rev-parse --show-toplevel 2^>nul`) do (
        set "SOURCE_ROOT=%%R"
    )
)

if "%SOURCE_ROOT%"=="" (
    for /f "usebackq tokens=*" %%R in (`git rev-parse --show-toplevel 2^>nul`) do (
        set "SOURCE_ROOT=%%R"
    )
)

if "%SOURCE_ROOT%"=="" (
    echo [SymbolStore] Could not find Git repository root. Source indexing skipped.
    set "ENABLE_SOURCE_INDEX=0"
) else (
    set "ENABLE_SOURCE_INDEX=1"
)

rem ------------------------------------------------------------
rem Resolve Git commit
rem ------------------------------------------------------------

if "%GIT_COMMIT%"=="" (
    if not "%SOURCE_ROOT%"=="" (
        for /f "usebackq tokens=*" %%G in (`git -C "%SOURCE_ROOT%" rev-parse HEAD 2^>nul`) do (
            set "GIT_COMMIT=%%G"
        )
    )
)

if "%GIT_COMMIT%"=="" (
    echo [SymbolStore] GIT_COMMIT is empty. Source indexing skipped.
    set "ENABLE_SOURCE_INDEX=0"
)

rem ------------------------------------------------------------
rem Normalize source root (ensure trailing backslash)
rem ------------------------------------------------------------

if not "%SOURCE_ROOT%"=="" (
    if not "%SOURCE_ROOT:~-1%"=="\" (
        set "SOURCE_ROOT=%SOURCE_ROOT%\"
    )
)

rem ------------------------------------------------------------
rem Print summary
rem ------------------------------------------------------------

echo ============================================================
echo  PostBuildSymbolStore
echo  Config       : %CONFIG%
echo  OutDir       : %OUT_DIR%
echo  ProjectDir   : %PROJECT_DIR%
echo  Product      : %PRODUCT%
echo  Symbol Store : %SYMBOL_STORE_PATH%
echo  Source Root  : %SOURCE_ROOT%
echo  Git Commit   : %GIT_COMMIT%
echo ============================================================

rem ------------------------------------------------------------
rem Process PDB files
rem ------------------------------------------------------------

set "FOUND_PDB=0"

for %%P in ("%OUT_DIR%*.pdb") do (
    if exist "%%~fP" (
        set "FOUND_PDB=1"
        set "PDB_PATH=%%~fP"
        set "PDB_NAME=%%~nxP"
        set "PDB_BASE=%%~nP"

        echo.
        echo ------------------------------------------------------------
        echo [SymbolStore] Processing !PDB_NAME!
        echo ------------------------------------------------------------

        if "!ENABLE_SOURCE_INDEX!"=="1" (
            call :WriteSourceIndex "!PDB_PATH!" "!PDB_BASE!"
            if errorlevel 1 (
                echo [SymbolStore] WARNING: Source indexing failed for !PDB_NAME!. Continuing.
            )
        ) else (
            echo [SymbolStore] Source indexing skipped.
        )

        echo [SymbolStore] Adding PDB to Symbol Store.
        "%SYMSTORE%" add /f "!PDB_PATH!" /s "%SYMBOL_STORE_PATH%" /t "%PRODUCT%" /v "%CONFIG%" /c "Post-build symbol upload"

        if errorlevel 1 (
            echo [SymbolStore] ERROR: symstore failed for !PDB_NAME!.
            exit /b 1
        )
    )
)

if "%FOUND_PDB%"=="0" (
    echo [SymbolStore] WARNING: No .pdb files found in "%OUT_DIR%"
    exit /b 0
)

echo.
echo [SymbolStore] All symbols uploaded successfully.
endlocal
exit /b 0


rem ============================================================
rem Subroutine: WriteSourceIndex
rem Args: %~1 = PDB path, %~2 = PDB base name
rem ============================================================

:WriteSourceIndex
set "PDB_PATH=%~1"
set "PDB_BASE=%~2"

set "TEMP_DIR=%TEMP%\JungleSymbolUpload"
set "SRC_LIST=%TEMP_DIR%\%PDB_BASE%_sources.txt"
set "SRCSRV_FILE=%TEMP_DIR%\%PDB_BASE%.srcsrv"

if not exist "%TEMP_DIR%" (
    mkdir "%TEMP_DIR%"
)

echo [SymbolStore] [1/3] Extract source list via srctool.exe
"%SRCTOOL%" "%PDB_PATH%" > "%SRC_LIST%" 2>nul

if errorlevel 1 (
    echo [SymbolStore] srctool.exe failed.
    exit /b 1
)

if not exist "%SRC_LIST%" (
    echo [SymbolStore] Source list was not created.
    exit /b 1
)

echo [SymbolStore] [2/3] Generate srcsrv stream file
set "SRCSRV_TRGT=%%targ%%\%%var2%%\%%fnbksl%%^(%%var3%%^)\%%var4%%\%%fnfile%%^(%%var1%%^)"
set "SRCSRV_CMD=cmd /c git -C ""%SOURCE_ROOT%"" show %%var4%%:%%var3%% ^> ""%%srcsrvtrg%%"""

(
    echo SRCSRV: ini ------------------------------------------------
    echo VERSION=1
    echo VERCTRL=Git
    echo DATETIME=%DATE% %TIME%
    echo SRCSRV: variables ------------------------------------------
    echo SRCSRVTRG=%SRCSRV_TRGT%
    echo SRCSRVCMD=%SRCSRV_CMD%
    echo SRCSRV: source files ---------------------------------------
) > "%SRCSRV_FILE%"

set "INDEXED_COUNT=0"

for /f "usebackq delims=" %%S in ("%SRC_LIST%") do (
    set "SRC_ABS=%%S"
    set "SRC_REL=!SRC_ABS:%SOURCE_ROOT%=!"

    if not "!SRC_REL!"=="!SRC_ABS!" (
        set "SRC_REL=!SRC_REL:\=/!"

        if "!SRC_REL:~0,1!"=="/" (
            set "SRC_REL=!SRC_REL:~1!"
        )

        echo !SRC_ABS!*%PRODUCT%*!SRC_REL!*%GIT_COMMIT%>> "%SRCSRV_FILE%"
        set /a INDEXED_COUNT+=1
    )
)

echo SRCSRV: end ------------------------------------------------>> "%SRCSRV_FILE%"

if "%INDEXED_COUNT%"=="0" (
    echo [SymbolStore] No source files indexed.
    exit /b 1
)

echo [SymbolStore] Indexed source files: %INDEXED_COUNT%

echo [SymbolStore] [3/3] Write srcsrv stream via pdbstr.exe
"%PDBSTR%" -w -p:"%PDB_PATH%" -s:srcsrv -i:"%SRCSRV_FILE%"

if errorlevel 1 (
    echo [SymbolStore] pdbstr.exe failed.
    exit /b 1
)

"%PDBSTR%" -r -p:"%PDB_PATH%" -s:srcsrv >nul 2>nul

if errorlevel 1 (
    echo [SymbolStore] srcsrv stream verification failed.
    exit /b 1
)

echo [SymbolStore] Source indexing complete.
exit /b 0
