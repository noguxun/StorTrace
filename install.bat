setlocal
rem should not start with \Device
rem set DEVICE=\Device\00000035
set DEVICE=\0000006a  
copy /y ".\x64\Debug\DiskTrace.sys" c:\windows\system32\drivers\*
.\AddFilter\x64\Debug\addfilter.exe /device %DEVICE%  /add DiskTrace
rem psshutdown /r /t 0