setlocal
REM should not start with \Device
set DEVICE=\00000069
copy /y ".\DiskTrace.sys" %SystemRoot%\system32\drivers\*

REM http://www.osronline.com/showthread.cfm?link=287922
sc create DiskTrace binPath= %SystemRoot%\System32\Drivers\DiskTrace.sys type= kernel  start= demand

REM upper filter does not have IRP_MJ_SCSI for read/write
addfilter.exe /device %DEVICE%  /add DiskTrace /lower

