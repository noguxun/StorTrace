/*++

Module Name:

    driver.c

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "driver.tmh"

#include "RingBuf.h"
//-------------------------------------------------------
// Variable Definition
//-------------------------------------------------------


//-------------------------------------------------------
// Imported Function & Variable Declaration
//-------------------------------------------------------
extern WDFCOLLECTION   DeviceCollection;
extern WDFWAITLOCK     DeviceCollectionLock;
extern WDFSPINLOCK     CdbBufSpinLock;


//-------------------------------------------------------
// Function Implementatjion
//-------------------------------------------------------

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#endif

VOID
DriverUnload(
    _In_
    WDFDRIVER Driver
)
{
    UNREFERENCED_PARAMETER(Driver);
    DbgPrint("Driver Unload \n");
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");    

    // 
    // Init ring buffer for data capture
    // TODO: make it per-device? 
    //
    RingBufReset();

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = StorTraceEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config,
                           StorTraceEvtDeviceAdd
                           );

    // config.DriverInitFlags = WdfDriverInitNonPnpDriver;
    config.EvtDriverUnload = DriverUnload;

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             WDF_NO_HANDLE
                             );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    //
    // Since there is only one control-device for all the instances
    // of the physical device, we need an ability to get to particular instance
    // of the device in our FilterEvtIoDeviceControlForControl. For that we
    // will create a collection object and store filter device objects.        
    // The collection object has the driver object as a default parent.
    //

    status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES,
        &DeviceCollection);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("WdfCollectionCreate failed with status 0x%x\n", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    //
    // The wait-lock object has the driver object as a default parent.
    //
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &DeviceCollectionLock);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("DeviceCollectionLock failed with status 0x%x\n", status);
        return status;
    }

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &CdbBufSpinLock);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("CdbBufSpinLock failed with status 0x%x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    DbgPrint("DriverEntry status 0x%x\n", status);
    return status;
}

NTSTATUS
StorTraceEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a device object to
    represent a new instance of the device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    DbgPrint("StorTraceEvtDeviceAdd\n");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = StorTraceCreateDevice(DeviceInit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

VOID
StorTraceEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
/*++
Routine Description:

    Free all the resources allocated in DriverEntry.

Arguments:

    DriverObject - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    UNREFERENCED_PARAMETER(DriverObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Stop WPP Tracing
    //
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)DriverObject));
}
