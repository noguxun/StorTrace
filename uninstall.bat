setlocal

FOR /F %%i IN (deviceName.txt) DO (
SET DEVICE=%%i
)

addfilter.exe /device %DEVICE%  /remove StorTrace  /lower
sc delete StorTrace
