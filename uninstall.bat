@echo off
setlocal

echo Remove filter from disk

addfilter.exe /remove StorTrace /lower
if  %errorlevel% neq 0 (
   echo Filter remove failed
   exit /B 1
)


echo Uninstall services

rem sc stop StorTrace
rem if  %errorlevel% neq 0 (
rem   echo Service stop failed, continue to delete however
rem )

sc delete StorTrace
if  %errorlevel% neq 0 (
   echo Service Uninstallation failed
   exit /B 1
)
