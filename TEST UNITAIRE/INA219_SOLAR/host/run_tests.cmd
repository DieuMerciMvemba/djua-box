@echo off
setlocal
rem Executer depuis une invite developpeur Visual Studio.
set "DJUA_SOLAR_BUILD=%TEMP%\djua-solar-host"
if not exist "%DJUA_SOLAR_BUILD%" mkdir "%DJUA_SOLAR_BUILD%"
pushd "%DJUA_SOLAR_BUILD%"
call :variant solar 1 1 1
if not "%errorlevel%"=="0" goto failed
call :variant reversed 1 -1 1
if not "%errorlevel%"=="0" goto failed
call :variant silent 1 1 0
if not "%errorlevel%"=="0" goto failed
call :variant disabled 0 1 1
if not "%errorlevel%"=="0" goto failed
popd
exit /b 0
:variant
cl /nologo /std:c++17 /EHsc /W4 /DIOT_HARDWARE_V2=0 /I"%~dp0." /DBUZZER_ENABLED=%4 /DSOLAR_MEASUREMENT_ENABLED=%2 /DSOLAR_CURRENT_POLARITY=%3 "%~dp0solar_test.cpp" /Fe:%1.exe /Fo:%1.obj
if not "%errorlevel%"=="0" exit /b 1
%1.exe
if not "%errorlevel%"=="0" exit /b 1
exit /b 0
:failed
popd
exit /b 1
