@echo off
:: Elevate script to admin priv
set "params=%*"
cd /d "%~dp0" && ( if exist "%temp%\getadmin.vbs" del "%temp%\getadmin.vbs" ) && fsutil dirty query %systemdrive% 1>nul 2>nul || (  echo Set UAC = CreateObject^("Shell.Application"^) : UAC.ShellExecute "cmd.exe", "/k cd ""%~sdp0"" && %~s0 %params%", "", "runas", 1 >> "%temp%\getadmin.vbs" && "%temp%\getadmin.vbs" && exit /B )

:: Make changes for Windows build
::

:: libel\protocol\el.proto -> .submodules\embedded-lib-protocol\el.proto
DEL /AS libel\protocol\el.proto >nul 2>nul && COPY .submodules\embedded-lib-protocol\el.proto libel\protocol\el.proto >nul

:: sample_client_synergy\config.h
DEL /AS sample_client_synergy\config.h>nul 2>nul && COPY sample_client\config.h sample_client_synergy\config.h >nul

:: sample_client_synergy\Makefile
DEL /AS sample_client_synergy\Makefile >nul 2>nul && COPY sample_client\Makefile sample_client_synergy\Makefile >nul

:: sample_client_synergy\sample_client.conf
DEL /AS sample_client_synergy\sample_client.conf  >nul 2>nul && COPY sample_client\sample_client.conf sample_client_synergy\sample_client.conf >nul

:: sample_client_synergy\send.h
DEL /AS sample_client_synergy\send.h >nul 2>nul && COPY sample_client\send.h sample_client_synergy\send.h >nul

:: sample_client_synergy\protocol\el.options
COPY libel\protocol\el.options sample_client_synergy\protocol\el.options >nul

:: sample_client_synergy\protocol\el.proto
COPY .submodules\embedded-lib-protocol\el.proto sample_client_synergy\protocol\el.proto >nul

:: sample_client_synergy\protocol\Makefile
COPY libel\protocol\Makefile sample_client_synergy\protocol\Makefile >nul

:: sample_client_synergy\protocol\proto.c
COPY libel\protocol\proto.c sample_client_synergy\protocol\proto.c >nul

:: sample_client_synergy\protocol\proto.h
COPY libel\protocol\proto.h sample_client_synergy\protocol\proto.h >nul
