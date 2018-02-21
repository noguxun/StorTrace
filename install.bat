setlocal
rem should not start with \Device
rem set DEVICE=\Device\00000035
set DEVICE=\00000035  
copy /y ".\x64\Debug\StorTrace.sys" c:\windows\system32\drivers\*
.\AddFilter\x64\Debug\addfilter.exe /device %DEVICE%  /add StorTrace
rem psshutdown /r /t 0