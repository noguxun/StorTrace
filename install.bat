@echo off
setlocal

REM get the device name from device Name.txt
echo Copy StorTrace.sys to system folder
copy /y ".\StorTrace.sys" %SystemRoot%\system32\drivers\*
if  %errorlevel% neq 0 (
   echo Copy Failed, system reboot may be needed
   exit /B 1
)

REM http://www.osronline.com/showthread.cfm?link=287922
echo Install StorTrace service
sc create StorTrace binPath= %SystemRoot%\System32\Drivers\StorTrace.sys type= kernel  start= demand
if  %errorlevel% neq 0 (
   echo Service Installation failed
   exit /B 1
)


REM add StorTrace filter driver to lower filter of disk
REM upper filter does not have IRP_MJ_SCSI for read/write
echo Add StorTrace filter to disk
addfilter.exe /add StorTrace /lower



