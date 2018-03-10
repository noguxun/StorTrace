@ECHO OFF

SET install_dir="c:\\StorTrace\\"

if not exist %install_dir% mkdir %install_dir%

copy  x64\Debug\StorTrace.sys %install_dir%* /y
copy  x64\Debug\*.exe %install_dir%* /y
copy  *install.bat %install_dir% /y
copy  testEnvSetup.bat %install_dir% /y