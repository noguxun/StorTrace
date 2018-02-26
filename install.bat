setlocal

REM get the device name from device Name.txt
FOR /F %%i IN (deviceName.txt) DO (
SET DEVICE=%%i
)

copy /y ".\StorTrace.sys" %SystemRoot%\system32\drivers\*

REM http://www.osronline.com/showthread.cfm?link=287922
sc create StorTrace binPath= %SystemRoot%\System32\Drivers\StorTrace.sys type= kernel  start= demand

REM add StorTrace filter driver to lower filter of disk
REM upper filter does not have IRP_MJ_SCSI for read/write
addfilter.exe /device %DEVICE%  /add StorTrace /lower

