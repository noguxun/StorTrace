setlocal
set DEVICE=\00000069
addfilter.exe /device %DEVICE%  /remove StorTrace  /lower
sc delete StorTrace
