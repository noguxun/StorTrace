// StApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "assert.h"

HANDLE ControlDevice = INVALID_HANDLE_VALUE;

BOOL TraceGet(PUCHAR pData)
{    
    BOOL success;
    ULONG bytesReturned = 0;

    if (INVALID_HANDLE_VALUE == ControlDevice) {
        printf("control device not created\n");
        return FALSE;
    }

    // Copy byte by byte
    // TODO: optimize
    success = ReadFile(ControlDevice, pData, 1, &bytesReturned, NULL);
    if (!success || bytesReturned == 0)
    {
        // printf("Read failed \n");
        return FALSE;
    }    

    return TRUE;
}

void PrintTraceData(void)
{
    ULONG  bytesReturned = 0;
    PUCHAR readBuffer = NULL;

    if (INVALID_HANDLE_VALUE == ControlDevice) { 
        printf("control device not created\n");
        return; 
    }

    readBuffer = (PUCHAR)malloc(1000);

    while (TRUE)
    {        
        UCHAR data;
        UCHAR length;
        UCHAR cdb[16];

        // check magic number
        while (TRUE) 
        { 
            while (!TraceGet(&data)) { Sleep(500); }
            if (data != 0x20) {
                continue; 
            }

            while (!TraceGet(&data)) { Sleep(500); }                
            if (data != 0x18) {
                continue;
            }

            break;
        }        

        while (!TraceGet(&length)) { Sleep(200); }
        if (length > 32)
        {
            printf("CDB length is %d \n", length);
        }

        for (UCHAR i = 0; i < length; i++) {
            while (!TraceGet(&(cdb[i]))) { Sleep(200); }
        }
        
        printf("CDB %2d Bytes: ", length);

        for (UCHAR i = 0; i < length; i++) {
            printf("%02x ", cdb[i]);
        }
        printf("\n");

        if (INVALID_HANDLE_VALUE == ControlDevice) {
            printf("control device not created\n");
            goto EXIT;
        }
    }

EXIT:
    free(readBuffer);
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
            ControlDevice = INVALID_HANDLE_VALUE;
        }
        return FALSE;

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
        printf("Ioctl to StorTraceFilter device failed\n");
        return -1;
    }
    
    printf("Ioctl to StorTraceFilter device succeeded\n");
    
    //
    // Print the Trace data from device
    //
    PrintTraceData();

    CloseHandle(ControlDevice);
        
    return 0;
}

