@echo off
setlocal
rem Executer depuis une invite developpeur Visual Studio (compilateur cl).
set "DJUA_TEST_BUILD=%TEMP%\djua-indicators-host"
if not exist "%DJUA_TEST_BUILD%" mkdir "%DJUA_TEST_BUILD%"
pushd "%DJUA_TEST_BUILD%"
call :variant disabled 0 0 1
if not "%errorlevel%"=="0" goto failed
call :variant active_high 1 0 1
if not "%errorlevel%"=="0" goto failed
call :variant active_low 1 0 0
if not "%errorlevel%"=="0" goto failed
call :variant passive_high 1 1 1
if not "%errorlevel%"=="0" goto failed
call :variant passive_low 1 1 0
if not "%errorlevel%"=="0" goto failed
call :variant http_profile 0 0 1 3
if not "%errorlevel%"=="0" goto failed
call :variant mqtt_profile 0 0 1 2
if not "%errorlevel%"=="0" goto failed
popd
exit /b 0

:variant
echo Testing %1
set "DJUA_STATUS_PROFILE=%5"
if "%DJUA_STATUS_PROFILE%"=="" set "DJUA_STATUS_PROFILE=1"
cl /nologo /std:c++17 /EHsc /W4 /DIOT_HARDWARE_V2=0 /I"%~dp0." /DDJUA_COMM_PROFILE=%DJUA_STATUS_PROFILE% /DBUZZER_ENABLED=%2 /DBUZZER_PASSIVE=%3 /DBUZZER_ACTIVE_HIGH=%4 "%~dp0indicators_test.cpp" /Fe:%1.exe /Fo:%1.obj
if not "%errorlevel%"=="0" exit /b 1
%1.exe
if not "%errorlevel%"=="0" exit /b 1
exit /b 0

:failed
popd
exit /b 1
