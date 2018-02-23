setlocal
set DEVICE=\00000069
addfilter.exe /device %DEVICE%  /remove DiskTrace  /lower
sc delete DiskTrace
