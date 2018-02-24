setlocal
REM should not start with \Device
set DEVICE=\0000006a
copy /y ".\StorTrace.sys" %SystemRoot%\system32\drivers\*

REM http://www.osronline.com/showthread.cfm?link=287922
sc create StorTrace binPath= %SystemRoot%\System32\Drivers\StorTrace.sys type= kernel  start= demand

REM upper filter does not have IRP_MJ_SCSI for read/write
addfilter.exe /device %DEVICE%  /add StorTrace /lower

