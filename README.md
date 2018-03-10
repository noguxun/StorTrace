# StorTrace

A simple tool to capture SCSI CDB(Command Descriptor Block) sent to the disk.  
 

# How to build
Open the StorTrace.sln with Visual Studio and build. 
There are three parts: 
- StorTrace.sys is the driver, will be running in kernel as a filter to disk 
- AddFilter.exe, the tool attach driver to disk
- StApp.exe, application running on user mode to show captured CDB data

# How to install 
Open a command prompt with administrator primitives. 
### Copy Files
copy all binaries into a StorTrace folder
```
> copyToInstall.bat
> cd c:\StorTrace
```  
### Turn on Test Mode
Turn on Windows's test mode to disable driver signature enforcement, windows does not allow to install any driver without being signed. 
```
> testEnvSetup.bat
```
Reoot the PC after that. 
### Install Driver
Attach the driver to a disk, to which you want to monitor. 
You will see a list of disks, and you need input index of the disk. 
```
> cd c:\StorTrace
> install.bat
Install StorTrace service
[SC] CreateService SUCCESS
Add StorTrace filter to disk
Using Lower Filters
Disk 0
  Friendly Name: VMware Virtual NVMe Disk
  Description: Disk drive
  Device Name: \Device\0000006e
  Filter lower: None


Disk 1
  Friendly Name: VMware, VMware Virtual S SCSI Disk Device
  Description: Disk drive
  Device Name: \Device\00000069
  Filter lower: None


Disk 2
  Friendly Name: VMware, VMware Virtual S SCSI Disk Device
  Description: Disk drive
  Device Name: \Device\0000006a
  Filter lower: None


Input Device Index To Trace -->
2
Everything has completed normally
```
you might need to reboot the PC if you are promoted so. 

### Run App
```
> StApp.exe
Hello, StorTrace App
Ioctl to StorTraceFilter device succeeded
CDB 10 Bytes: 25 00 00 00 00 00 00 00 00 00
CDB  6 Bytes: 1a 00 1c 00 c0 00
CDB  6 Bytes: 12 01 00 00 ff 00
CDB  6 Bytes: 12 01 b1 00 40 00
CDB  6 Bytes: 1a 00 08 00 c0 00
CDB  6 Bytes: 1a 00 08 00 c0 00
CDB 16 Bytes: 9e 10 00 00 00 00 00 00 00 00 00 00 00 20 00 00
CDB 10 Bytes: 28 00 00 00 00 00 00 00 01 00
...
```


