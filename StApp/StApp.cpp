// StApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

HANDLE ControlDevice = INVALID_HANDLE_VALUE;

void PrintTraceData(void)
{
	if (INVALID_HANDLE_VALUE == ControlDevice) { 
		return; 
	}
}

BOOL CtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
		// Handle the CTRL-C signal. 
	case CTRL_C_EVENT:
		printf("Ctrl-C event\n\n");
		Beep(750, 300);

		if (ControlDevice != INVALID_HANDLE_VALUE) {
			CloseHandle(ControlDevice);
		}
		return(TRUE);

	default:
		return FALSE;
	}
}

int main()
{
	BOOL success;
	printf("Hello, StorTrace App\n");

	success = SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
	if (!success)
	{
		printf("Failed to set console handler\n");
		return -1;
	}


	HANDLE ControlDevice;
	ULONG  bytes;

	//
	// Open handle to the control device. Please note that even
	// a non-admin user can open handle to the device with
	// FILE_READ_ATTRIBUTES | SYNCHRONIZE DesiredAccess and send IOCTLs if the
	// IOCTL is defined with FILE_ANY_ACCESS. So for better security avoid
	// specifying FILE_ANY_ACCESS in your IOCTL defintions.
	// If the IOCTL is defined to have FILE_READ_DATA access rights, you can
	// open the device with GENERIC_READ and call DeviceIoControl.
	// If the IOCTL is defined to have FILE_WRITE_DATA access rights, you can
	// open the device with GENERIC_WRITE and call DeviceIoControl.
	//
	ControlDevice = CreateFile(TEXT("\\\\.\\StorTraceFilter"),
		GENERIC_READ, // Only read access
		0, // FILE_SHARE_READ | FILE_SHARE_WRITE
		NULL, // no SECURITY_ATTRIBUTES structure
		OPEN_EXISTING, // No special create flags
		0, // No special attributes
		NULL); // No template file

	if (INVALID_HANDLE_VALUE == ControlDevice) {
		printf("Failed to open StorTraceFilter device");
		return -1;
	}
	
	success = DeviceIoControl(ControlDevice,
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0, METHOD_BUFFERED, FILE_READ_DATA),
		NULL, 0,
		NULL, 0,
		&bytes, NULL);
	if (!success)
	{
		printf("Ioctl to StorTraceFilter device failed");
		return -1;
	}
	
	printf("Ioctl to StorTraceFilter device succeeded");
	
	//
	// Print the Trace data from device
	//
	PrintTraceData();

	CloseHandle(ControlDevice);
		
    return 0;
}

