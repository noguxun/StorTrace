setlocal
set DEVICE=\0000006a
addfilter.exe /device %DEVICE%  /remove StorTrace  /lower
sc delete StorTrace
