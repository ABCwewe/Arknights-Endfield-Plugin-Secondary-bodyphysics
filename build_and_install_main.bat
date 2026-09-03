@echo off
setlocal
set "ROOT=%~dp0"
set "GAMEROOT=E:\GAMU\Hypergryph Launcher\games\Endfield Game"
if not "%~1"=="" set "GAMEROOT=%~1"
set "GAMEPLUGIN=%GAMEROOT%\plugin"

tasklist /fo csv | findstr /i /c:"Endfield.exe" >nul
if not errorlevel 1 (
    echo [ERROR] Close Endfield.exe before install.
    pause
    exit /b 1
)
tasklist /fo csv | findstr /i /c:"SecondaryMotion.Manager.exe" >nul
if not errorlevel 1 (
    echo [ERROR] Close SecondaryMotion.Manager.exe before install.
    pause
    exit /b 1
)
if not exist "%GAMEROOT%\Endfield.exe" if not exist "%GAMEROOT%\UnityPlayer.dll" (
    echo [ERROR] Game root marker not found: %GAMEROOT%
    pause
    exit /b 1
)

pushd "%ROOT%"
echo [1/4] Building current main source...
call build.bat <nul
if errorlevel 1 (
    echo [ERROR] Build failed.
    popd
    pause
    exit /b 1
)

for %%F in (sbm.dll d3dcompiler_47.dll vulkan-1.dll) do (
    if not exist "bin\%%F" (
        echo [ERROR] Missing bin\%%F
        popd
        pause
        exit /b 1
    )
)

echo [2/4] Refreshing the source tool plugin folder...
if not exist "SecondaryMotion\plugin" mkdir "SecondaryMotion\plugin"
copy /y "bin\sbm.dll" "SecondaryMotion\plugin\sbm.dll" >nul
copy /y "bin\d3dcompiler_47.dll" "SecondaryMotion\plugin\d3dcompiler_47.dll" >nul
copy /y "bin\vulkan-1.dll" "SecondaryMotion\plugin\vulkan-1.dll" >nul

echo [3/4] Backing up and installing game DLLs...
if not exist "%GAMEPLUGIN%" mkdir "%GAMEPLUGIN%"
if exist "%GAMEPLUGIN%\sbm.dll" copy /y "%GAMEPLUGIN%\sbm.dll" "%GAMEPLUGIN%\sbm.dll.pre_source_install" >nul
if exist "%GAMEROOT%\d3dcompiler_47.dll" copy /y "%GAMEROOT%\d3dcompiler_47.dll" "%GAMEROOT%\d3dcompiler_47.dll.pre_source_install" >nul
if exist "%GAMEROOT%\vulkan-1.dll" copy /y "%GAMEROOT%\vulkan-1.dll" "%GAMEROOT%\vulkan-1.dll.pre_source_install" >nul
copy /y "bin\sbm.dll" "%GAMEPLUGIN%\sbm.dll" >nul
copy /y "bin\d3dcompiler_47.dll" "%GAMEROOT%\d3dcompiler_47.dll" >nul
copy /y "bin\vulkan-1.dll" "%GAMEROOT%\vulkan-1.dll" >nul

echo [4/4] Verifying...
fc /b "bin\sbm.dll" "%GAMEPLUGIN%\sbm.dll" >nul
if errorlevel 1 goto verify_fail
fc /b "bin\d3dcompiler_47.dll" "%GAMEROOT%\d3dcompiler_47.dll" >nul
if errorlevel 1 goto verify_fail
fc /b "bin\vulkan-1.dll" "%GAMEROOT%\vulkan-1.dll" >nul
if errorlevel 1 goto verify_fail
popd
echo.
echo INSTALL=PASS
echo Main source build is active in the game folder.
pause
exit /b 0

:verify_fail
popd
echo [ERROR] Installed file verification failed.
pause
exit /b 1
