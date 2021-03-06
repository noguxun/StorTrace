// StApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "assert.h"


#define _NTSCSI_USER_MODE_
#include "scsi.h"

#define TGET(data)       while (!TraceGet(&(data))) { Sleep(500); }

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

    if (INVALID_HANDLE_VALUE == ControlDevice) { 
        printf("control device not created\n");
        return; 
    }

    while (TRUE)
    {        
        UCHAR data;
        UCHAR cdbLength;
        UCHAR senseLength;
        NTSTATUS ntStatus = 0;
        UCHAR scsiStatus = 0;

        // check magic number 0xDEAF
        while (TRUE) 
        { 
            TGET(data);            
            if (data != 0xDE) {
                printf("missed sync code: expect %X but got %X \n", 0xDE, data);
                continue; 
            }

            TGET(data);            
            if (data != 0xAF) {
                printf("missed sync code: expect %X but got %X \n", 0xAF, data);
                continue;
            }

            break;
        }        

        // Get NT status
        assert(sizeof(ntStatus) == 4);
        TGET(data);  ntStatus |= data;
        TGET(data);  ntStatus |= (data << 8);
        TGET(data);  ntStatus |= (data << 16);
        TGET(data);  ntStatus |= (data << 24);

        // Get Scsi status
        TGET(scsiStatus);

        TGET(cdbLength);        
        if (cdbLength > 32)
        {
            printf("CDB length is %d, Abnormal!!\n", cdbLength);
        }

        TGET(senseLength);        
        if (senseLength > 100)
        {
            printf("CDB length is %d, Abnormal!!\n", senseLength);
        }

        printf("CDB %2d Bytes: ", cdbLength);

        for (UCHAR i = 0; i < cdbLength; i++) {
            TGET(data);            
            printf("%02X ", data);
        }        
        printf("\n");

        if (scsiStatus || ntStatus)
        {
            printf("    NtStatus: %08X \n", ntStatus);
            printf("    ScsiStatus: %08X \n", scsiStatus);
        }

        if (senseLength)
        {            
            UCHAR senseKey;
            UCHAR adSenseCode;
            UCHAR adSenseCodeQual;
            BOOLEAN validSense;
            PUCHAR senseData = (PUCHAR)malloc(senseLength * sizeof(UCHAR));

            // Need to read it out
            for (UCHAR i = 0; i < senseLength; i++) {
                TGET(senseData[i]);
            }

            // only to print it if 
            if (scsiStatus)
            {
                validSense = ScsiGetSenseKeyAndCodes(senseData,
                    senseLength,
                    SCSI_SENSE_OPTIONS_FIXED_FORMAT_IF_UNKNOWN_FORMAT_INDICATED,
                    &senseKey,
                    &adSenseCode,
                    &adSenseCodeQual);

                printf("    Sense Key: %02X \n", senseKey);
                printf("    Sense Code: %02X %02X \n", adSenseCode, adSenseCodeQual);
                printf("    Sense Raw %d Bytes:", senseLength);

                // for Toshiba SCSI device, the format is of "Fixed format sense data"
                // Howerver the response code is FF, not 70 or 71.
                for (UCHAR i = 0; i < senseLength; i++) {
                    if (i % 16 == 0)
                    {
                        printf("\n        ");
                    }
                    printf("%02X ", senseData[i]);
                }
                printf("\n");                
            }

            free(senseData);
        }
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

