@echo off
echo ========================================
echo Building Menu Base - All Configurations
echo ========================================
echo.

REM Get the directory where this script is located
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM Find MSBuild.exe (check Visual Studio 2026 Insiders first)
set MSBUILD_PATH=

REM Try Visual Studio 2026 Insiders
if exist "D:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD_PATH=D:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe
    goto :found_msbuild
)

REM Try Visual Studio 2026 Insiders (C: drive)
if exist "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe
    goto :found_msbuild
)

REM Try Visual Studio 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe
    goto :found_msbuild
)

REM Try Visual Studio 2022 Professional
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe
    goto :found_msbuild
)

REM Try Visual Studio 2022 Enterprise
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe
    goto :found_msbuild
)

REM Try Visual Studio 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe
    goto :found_msbuild
)

REM Try using vswhere to find MSBuild
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2^>nul`) do (
    set MSBUILD_PATH=%%i
    goto :found_msbuild
)

echo ERROR: Could not find MSBuild.exe
echo Please ensure Visual Studio is installed
pause
exit /b 1

:found_msbuild
echo Using MSBuild: %MSBUILD_PATH%
echo.

REM Try to find and set up vcpkg environment
REM Check common vcpkg locations
set VCPKG_FOUND=0

REM Check if VCPKG_ROOT is already set
if defined VCPKG_ROOT (
    echo Using existing VCPKG_ROOT: %VCPKG_ROOT%
    set VCPKG_FOUND=1
    goto :vcpkg_setup
)

REM Check common vcpkg installation paths
if exist "%USERPROFILE%\vcpkg\vcpkg.exe" (
    set VCPKG_ROOT=%USERPROFILE%\vcpkg
    set VCPKG_FOUND=1
    goto :vcpkg_setup
)

if exist "C:\vcpkg\vcpkg.exe" (
    set VCPKG_ROOT=C:\vcpkg
    set VCPKG_FOUND=1
    goto :vcpkg_setup
)

if exist "D:\vcpkg\vcpkg.exe" (
    set VCPKG_ROOT=D:\vcpkg
    set VCPKG_FOUND=1
    goto :vcpkg_setup
)

REM Try to find vcpkg from Visual Studio integration
REM vcpkg integrate install modifies user.props which MSBuild will use automatically
REM So if vcpkg is integrated, MSBuild should handle it automatically

:vcpkg_setup
if %VCPKG_FOUND%==1 (
    echo Found vcpkg at: %VCPKG_ROOT%
    echo Note: MSBuild should automatically use vcpkg if 'vcpkg integrate install' was run
) else (
    echo Note: vcpkg not found in common locations. If you use vcpkg, ensure 'vcpkg integrate install' was run.
    echo       MSBuild will use vcpkg integration if it's set up in Visual Studio.
)
echo.

REM Build Dev x64 Configuration
echo ========================================
echo Building: Dev x64
echo ========================================
"%MSBUILD_PATH%" "Menu Base.vcxproj" /t:Build /p:Configuration=Dev /p:Platform=x64 /p:VcpkgEnabled=true /p:CL=/FS /m
if errorlevel 1 (
    echo.
    echo ERROR: Dev x64 build failed!
    pause
    exit /b 1
)
echo.
echo Dev x64 build completed successfully!
echo.

REM Copy vcpkg DLLs for Dev configuration if vcpkg is found
if %VCPKG_FOUND%==1 (
    echo Copying vcpkg DLLs for Dev configuration...
    if exist "%VCPKG_ROOT%\installed\x64-windows\bin\*.dll" (
        xcopy /Y /I "%VCPKG_ROOT%\installed\x64-windows\bin\*.dll" "bin\Dev\x64\" >nul 2>&1
        echo   Copied DLLs from vcpkg installed directory
    )
)

echo.

REM Build Release x64 Configuration
echo ========================================
echo Building: Release x64
echo ========================================
"%MSBUILD_PATH%" "Menu Base.vcxproj" /t:Build /p:Configuration=Release /p:Platform=x64 /p:VcpkgEnabled=true /p:CL=/FS /m
if errorlevel 1 (
    echo.
    echo ERROR: Release x64 build failed!
    pause
    exit /b 1
)
echo.
echo Release x64 build completed successfully!
echo.

REM Copy vcpkg DLLs for Release configuration if vcpkg is found
if %VCPKG_FOUND%==1 (
    echo Copying vcpkg DLLs for Release configuration...
    if exist "%VCPKG_ROOT%\installed\x64-windows\bin\*.dll" (
        xcopy /Y /I "%VCPKG_ROOT%\installed\x64-windows\bin\*.dll" "bin\Release\x64\" >nul 2>&1
        echo   Copied DLLs from vcpkg installed directory
    )
)

echo.

echo ========================================
echo All builds completed successfully!
echo ========================================
echo.
echo Output locations:
echo   Dev x64:    bin\Dev\x64\Menu Base.exe
echo   Release x64: bin\Release\x64\Menu Base.exe
echo.
echo Note: If DLLs are missing, ensure:
echo   1. Run 'vcpkg integrate install' to enable MSBuild integration
echo   2. Or manually copy DLLs from vcpkg\installed\x64-windows\bin to the output folders
echo.
pause
