@echo off
rem ---------------------------------------------------------------------------
rem idf.bat - run any idf.py command with the ESP-IDF v5.5.5 environment loaded.
rem
rem   tools\idf.bat build
rem   tools\idf.bat -p COM49 flash monitor
rem   tools\idf.bat menuconfig
rem
rem Which project is built comes from IDF_PROJECT_DIR, defaulting to firmware\.
rem
rem Override the install locations with IDF_PATH / IDF_TOOLS_PATH if yours live
rem somewhere else.
rem ---------------------------------------------------------------------------
setlocal

if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
if "%IDF_PATH%"=="" set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.5
if "%IDF_PROJECT_DIR%"=="" set IDF_PROJECT_DIR=%~dp0..\firmware

if not exist "%IDF_PATH%\export.bat" (
    echo ERROR: ESP-IDF not found at "%IDF_PATH%"
    echo        Set IDF_PATH to your esp-idf folder and retry.
    exit /b 1
)
if not exist "%IDF_PROJECT_DIR%\CMakeLists.txt" (
    echo ERROR: no CMakeLists.txt in "%IDF_PROJECT_DIR%"
    exit /b 1
)

call "%IDF_PATH%\export.bat" >nul
if errorlevel 1 (
    echo ERROR: "%IDF_PATH%\export.bat" failed
    exit /b 1
)

pushd "%IDF_PROJECT_DIR%"
idf.py %*
set rc=%errorlevel%
popd
exit /b %rc%
