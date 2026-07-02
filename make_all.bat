@echo off
setlocal EnableExtensions
chcp 65001 > nul

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "SCRIPT_PATH=%~f0"
set "RELEASE_DIR=%PROJECT_ROOT%\Release"
set "RELEASE_BIN=%RELEASE_DIR%\bin"
set "STAGE2_DIR=%PROJECT_ROOT%\Stage2-NativeNT"
set "STAGE2_EXE=%STAGE2_DIR%\objfre_win7_amd64\amd64\native.exe"
set "STAGE2_LOCAL_COPY=%STAGE2_DIR%\nativex64.exe"
set "STAGE3_SOLUTION=%PROJECT_ROOT%\Stage3-MsvpBypass\GrabAccessMsvpBypass.sln"
set "STAGE3_DLL=%PROJECT_ROOT%\Stage3-MsvpBypass\x64\Release\GrabAccessMsvpBypass.dll"
set "INJECTOR_PROJECT=%PROJECT_ROOT%\Stage3-Injector\Stage3-Injector.vcxproj"
set "INJECTOR_EXE=%PROJECT_ROOT%\Stage3-Injector\x64\Release\Injector.exe"
set "EXPLORER_HOST_PROJECT=%PROJECT_ROOT%\Stage3-ExplorerHost\Stage3-ExplorerHost.vcxproj"
set "EXPLORER_HOST_EXE=%PROJECT_ROOT%\Stage3-ExplorerHost\x64\Release\GrabAccessExplorerHost.exe"
set "FALLBACK_PROJECT=%PROJECT_ROOT%\Stage3-Fallback\Stage3-Fallback.vcxproj"
set "FALLBACK_EXE=%PROJECT_ROOT%\Stage3-Fallback\x64\Release\GrabAccessFallback.exe"

set "BUILD_STAGE2=1"
set "BUILD_STAGE3=1"
set "BUILD_INJECTOR=1"
set "BUILD_EXPLORER_HOST=1"
set "BUILD_FALLBACK=1"
set "DO_PACKAGE=1"
set "SKIP_SIGN=0"
set "NO_PAUSE=0"
set "NO_UAC=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--stage2-only" (
    set "BUILD_STAGE2=1"
    set "BUILD_STAGE3=0"
    set "BUILD_INJECTOR=0"
    set "BUILD_EXPLORER_HOST=0"
    set "BUILD_FALLBACK=0"
    set "DO_PACKAGE=0"
) else if /I "%~1"=="--stage3-only" (
    set "BUILD_STAGE2=0"
    set "BUILD_STAGE3=1"
    set "BUILD_INJECTOR=0"
    set "BUILD_EXPLORER_HOST=0"
    set "BUILD_FALLBACK=0"
    set "DO_PACKAGE=0"
) else if /I "%~1"=="--injector-only" (
    set "BUILD_STAGE2=0"
    set "BUILD_STAGE3=0"
    set "BUILD_INJECTOR=1"
    set "BUILD_EXPLORER_HOST=0"
    set "BUILD_FALLBACK=0"
    set "DO_PACKAGE=0"
) else if /I "%~1"=="--explorerhost-only" (
    set "BUILD_STAGE2=0"
    set "BUILD_STAGE3=0"
    set "BUILD_INJECTOR=0"
    set "BUILD_EXPLORER_HOST=1"
    set "BUILD_FALLBACK=0"
    set "DO_PACKAGE=0"
) else if /I "%~1"=="--fallback-only" (
    set "BUILD_STAGE2=0"
    set "BUILD_STAGE3=0"
    set "BUILD_INJECTOR=0"
    set "BUILD_EXPLORER_HOST=0"
    set "BUILD_FALLBACK=1"
    set "DO_PACKAGE=0"
) else if /I "%~1"=="--package-only" (
    set "BUILD_STAGE2=0"
    set "BUILD_STAGE3=0"
    set "BUILD_INJECTOR=0"
    set "BUILD_EXPLORER_HOST=0"
    set "BUILD_FALLBACK=0"
    set "DO_PACKAGE=1"
) else if /I "%~1"=="--skip-sign" (
    set "SKIP_SIGN=1"
) else if /I "%~1"=="--no-pause" (
    set "NO_PAUSE=1"
) else if /I "%~1"=="--no-uac" (
    set "NO_UAC=1"
) else if /I "%~1"=="--help" (
    goto usage
) else (
    echo [ERROR] Unknown option: %~1
    goto error_exit
)
shift
goto parse_args

:args_done
if "%DO_PACKAGE%"=="1" if "%SKIP_SIGN%"=="0" call :ensure_admin %*
if errorlevel 2 exit /b 0
if errorlevel 1 goto error_exit

echo =========================================================
echo GrabAccess one-click build
echo =========================================================
echo Project : %PROJECT_ROOT%
echo.

if not exist "%RELEASE_BIN%" mkdir "%RELEASE_BIN%"

if "%BUILD_STAGE3%%BUILD_INJECTOR%%BUILD_EXPLORER_HOST%%BUILD_FALLBACK%"=="0000" goto msbuild_done
call :detect_msbuild
if errorlevel 1 goto error_exit
:msbuild_done

if "%BUILD_STAGE2%"=="1" call :detect_winddk
if errorlevel 1 goto error_exit

if "%BUILD_INJECTOR%"=="1" call :build_injector
if errorlevel 1 goto error_exit

if "%BUILD_EXPLORER_HOST%"=="1" call :build_explorerhost
if errorlevel 1 goto error_exit

if "%BUILD_FALLBACK%"=="1" call :build_fallback
if errorlevel 1 goto error_exit

if "%BUILD_STAGE3%"=="1" call :build_stage3
if errorlevel 1 goto error_exit

if "%BUILD_STAGE2%"=="1" call :build_stage2
if errorlevel 1 goto error_exit

if "%DO_PACKAGE%"=="1" call :package_release
if errorlevel 1 goto error_exit

echo.
echo =========================================================
echo [OK] Build completed successfully.
echo =========================================================
if "%NO_PAUSE%"=="0" pause
exit /b 0

:usage
echo Usage: make_all.bat [options]
echo.
echo Options:
echo   --stage2-only    Build Stage2 only.
echo   --stage3-only    Build Stage3 only.
echo   --injector-only  Build Stage3-Injector only.
echo   --explorerhost-only Build Stage3-ExplorerHost only.
echo   --fallback-only Build Stage3-Fallback only.
echo   --package-only   Package Release\native.exe from Release\bin.
echo   --skip-sign      Package without Authenticode signing.
echo   --no-pause       Do not pause before exit.
echo.
exit /b 0

:ensure_admin
net session > nul 2>&1
if not errorlevel 1 exit /b 0
if "%NO_UAC%"=="1" (
    echo [ERROR] Administrator rights are required for signing.
    exit /b 1
)
echo [*] Requesting administrator rights for signing...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%ComSpec%' -ArgumentList '/c ""%SCRIPT_PATH%"" --no-uac %*' -Verb RunAs"
exit /b 2

:detect_msbuild
set "MSBUILD_PATH="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        if not defined MSBUILD_PATH set "MSBUILD_PATH=%%I"
    )
)
if not defined MSBUILD_PATH if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_PATH if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_PATH if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_PATH if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe" set "MSBUILD_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe"

if not defined MSBUILD_PATH (
    echo [ERROR] MSBuild not found. Install Visual Studio 2017+ with C++ tools.
    exit /b 1
)
echo [*] MSBuild: %MSBUILD_PATH%
exit /b 0

:detect_winddk
set "WINDDK_PATH="
if defined WINDDK if exist "%WINDDK%\bin\setenv.bat" set "WINDDK_PATH=%WINDDK%"
if not defined WINDDK_PATH (
    for /d %%D in ("%SystemDrive%\WinDDK"\*) do (
        if exist "%%~fD\bin\setenv.bat" if not defined WINDDK_PATH set "WINDDK_PATH=%%~fD"
    )
)
if not defined WINDDK_PATH (
    echo [ERROR] WinDDK not found. Install Windows Driver Kit 7.1.0 under C:\WinDDK.
    exit /b 1
)
echo [*] WinDDK : %WINDDK_PATH%
exit /b 0

:build_stage3
echo.
echo [4/6] Building Stage3 GrabAccessMsvpBypass.dll...
"%MSBUILD_PATH%" "%STAGE3_SOLUTION%" /p:Configuration=Release /p:Platform=x64 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] Stage3 build failed.
    exit /b 1
)
if not exist "%STAGE3_DLL%" (
    echo [ERROR] Stage3 output missing: %STAGE3_DLL%
    exit /b 1
)
copy /Y "%STAGE3_DLL%" "%RELEASE_BIN%\GrabAccessMsvpBypass.dll" > nul
echo [OK] Stage3 copied to Release\bin.
exit /b 0

:build_injector
echo.
echo [1/6] Building Stage3-Injector Injector.exe...
"%MSBUILD_PATH%" "%INJECTOR_PROJECT%" /p:Configuration=Release /p:Platform=x64 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] Injector build failed.
    exit /b 1
)
if not exist "%INJECTOR_EXE%" (
    echo [ERROR] Injector output missing: %INJECTOR_EXE%
    exit /b 1
)
copy /Y "%INJECTOR_EXE%" "%RELEASE_BIN%\Injector.exe" > nul
echo [OK] Injector copied to Release\bin.
exit /b 0

:build_explorerhost
echo.
echo [2/6] Building Stage3-ExplorerHost GrabAccessExplorerHost.exe...
"%MSBUILD_PATH%" "%EXPLORER_HOST_PROJECT%" /p:Configuration=Release /p:Platform=x64 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] Explorer host build failed.
    exit /b 1
)
if not exist "%EXPLORER_HOST_EXE%" (
    echo [ERROR] Explorer host output missing: %EXPLORER_HOST_EXE%
    exit /b 1
)
copy /Y "%EXPLORER_HOST_EXE%" "%RELEASE_BIN%\GrabAccessExplorerHost.exe" > nul
echo [OK] Explorer host copied to Release\bin.
exit /b 0

:build_fallback
echo.
echo [3/6] Building Stage3-Fallback GrabAccessFallback.exe...
"%MSBUILD_PATH%" "%FALLBACK_PROJECT%" /p:Configuration=Release /p:Platform=x64 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] Fallback build failed.
    exit /b 1
)
if not exist "%FALLBACK_EXE%" (
    echo [ERROR] Fallback output missing: %FALLBACK_EXE%
    exit /b 1
)
copy /Y "%FALLBACK_EXE%" "%RELEASE_BIN%\GrabAccessFallback.exe" > nul
echo [OK] Fallback copied to Release\bin.
exit /b 0

:build_stage2
echo.
echo [5/6] Building Stage2 NativeNT native.exe...
if exist "%STAGE2_EXE%" del /F /Q "%STAGE2_EXE%" > nul 2>&1
set "TEMP_BUILD=%TEMP%\grabaccess_stage2_build_%RANDOM%%RANDOM%.bat"
(
    echo @echo off
    echo call "%WINDDK_PATH%\bin\setenv.bat" %WINDDK_PATH% fre x64 WIN7
    echo cd /d "%STAGE2_DIR%"
    echo build /w /g ^> build_log.txt 2^>^&1
) > "%TEMP_BUILD%"
cmd.exe /c call "%TEMP_BUILD%"
set "STAGE2_ERROR=%ERRORLEVEL%"

if not "%STAGE2_ERROR%"=="0" (
    echo [ERROR] Stage2 build failed. Check Stage2-NativeNT\build_log.txt.
    echo [ERROR] Temporary script kept for debugging: %TEMP_BUILD%
    exit /b 1
)
del /F /Q "%TEMP_BUILD%" > nul 2>&1
if not exist "%STAGE2_EXE%" (
    echo [ERROR] Stage2 output missing: %STAGE2_EXE%
    exit /b 1
)
copy /Y "%STAGE2_EXE%" "%STAGE2_LOCAL_COPY%" > nul
copy /Y "%STAGE2_EXE%" "%RELEASE_BIN%\nativex64.exe" > nul
echo [OK] Stage2 copied to Stage2-NativeNT and Release\bin.
exit /b 0

:package_release
echo.
echo [6/6] Packaging Release\native.exe...
set "PACKAGE_ARGS=-NoUac -NoPause"
if "%SKIP_SIGN%"=="1" set "PACKAGE_ARGS=%PACKAGE_ARGS% -SkipSign"
powershell -NoProfile -ExecutionPolicy Bypass -File "%RELEASE_DIR%\build.ps1" %PACKAGE_ARGS%
if errorlevel 1 (
    echo [ERROR] Release packaging failed.
    exit /b 1
)
exit /b 0

:error_exit
echo.
echo =========================================================
echo [FAILED] Build aborted.
echo =========================================================
if "%NO_PAUSE%"=="0" pause
exit /b 1
