copy  x64\Debug\StorTrace.* c:\Temp\* /y
copy  x64\Debug\*.exe c:\Temp\* /y
copy  *install.bat c:\Temp\ /y

IF NOT EXIST "c:\Temp\deviceName.txt" (
copy  deviceName.txt c:\Temp\
)