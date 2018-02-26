/*++

Module Name:

    device.c - Device handling events for example driver.

Abstract:

   This file contains the device entry points and callbacks.
    
Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "device.tmh"

#include "wdfobject.h"

//-------------------------------------------------------
// MACRO 
//-------------------------------------------------------
#define NTDEVICE_NAME_STRING      L"\\Device\\StorTraceFilter"
#define SYMBOLIC_NAME_STRING      L"\\DosDevices\\StorTraceFilter"

//-------------------------------------------------------
// Variable Definition
//-------------------------------------------------------
//
// Collection object is used to store all the FilterDevice objects so
// that any event callback routine can easily walk thru the list and pick a
// specific instance of the device for filtering.
//
WDFCOLLECTION   DeviceCollection;
WDFWAITLOCK     DeviceCollectionLock;
WDFDEVICE       ControlDevice = NULL;

//-------------------------------------------------------
// Imported Function & Variable Declaration
//-------------------------------------------------------
NTSTATUS
StorTraceCreateControlDevice(
	_In_ WDFDEVICE Device
);



//-------------------------------------------------------
// Function Implementatjion
//-------------------------------------------------------
NTSTATUS
StorTraceCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++

Routine Description:

    Worker routine called to create a device and its software resources.

Arguments:

    DeviceInit - Pointer to an opaque init structure. Memory for this
                    structure will be freed by the framework when the WdfDeviceCreate
                    succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    NTSTATUS status;


	DbgPrint("%s\n", __FUNCTION__);

	//
	// Tell the framework that you are filter driver. Framework
	// takes care of inherting all the device flags & characterstics
	// from the lower device you are attaching to.
	//
	WdfFdoInitSetFilter(DeviceInit);


	//
	// Specify the size of device extension where we track per device
	// context.
	//
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM deviceobject, attach to the lower stack and set the
	// appropriate flags and attributes.
	//
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

	if (!NT_SUCCESS(status)) {
		return status;
	}

    //
    // Get a pointer to the device context structure that we just associated
    // with the device object. We define this structure in the device.h
    // header file. DeviceGetContext is an inline function generated by
    // using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
    // This function will do the type checking and return the device context.
    // If you pass a wrong object handle it will return NULL and assert if
    // run under framework verifier mode.
    //
    deviceContext = DeviceGetContext(device);

    //
    // Initialize the context.
    //
    deviceContext->SerialNo = 0x19771220;

    //
    // Create a device interface so that applications can find and talk
    // to us.
    //
    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_StorTrace,
        NULL // ReferenceString
        );

    if (NT_SUCCESS(status)) {
        //
        // Initialize the I/O Package and any Queues
        //
        status = StorTraceQueueInitialize(device);
    }
    

	WdfWaitLockAcquire(DeviceCollectionLock, NULL);
	//
	// WdfCollectionAdd takes a reference on the item object and removes
	// it when you call WdfCollectionRemove.
	//
	status = WdfCollectionAdd(DeviceCollection, device);
	if (!NT_SUCCESS(status)) {
		KdPrint(("WdfCollectionAdd failed with status code 0x%x\n", status));
	}
	WdfWaitLockRelease(DeviceCollectionLock);

	//
	// Create a control device
	//
	status = StorTraceCreateControlDevice(device);
	if (!NT_SUCCESS(status)) {
		DbgPrint("CreateControlDevice failed with status 0x%x\n",	status);
		//
		// Let us not fail AddDevice just because we weren't able to create the
		// control device.
		//
		status = STATUS_SUCCESS;
	}

    return status;
}

NTSTATUS
StorTraceCreateControlDevice(
	_In_ WDFDEVICE Device
)
{
	
	PWDFDEVICE_INIT             pInit = NULL;
	WDFDEVICE                   controlDevice = NULL;
	WDF_OBJECT_ATTRIBUTES       controlAttributes;
	
	BOOLEAN                     bCreate = FALSE;
	NTSTATUS                    status;
	
	DECLARE_CONST_UNICODE_STRING(ntDeviceName, NTDEVICE_NAME_STRING);
	DECLARE_CONST_UNICODE_STRING(symbolicLinkName, SYMBOLIC_NAME_STRING);

	
	//
	// First find out whether any ControlDevice has been created. If the
	// collection has more than one device then we know somebody has already
	// created or in the process of creating the device.
	//
	WdfWaitLockAcquire(DeviceCollectionLock, NULL);

	if (WdfCollectionGetCount(DeviceCollection) == 1) {
		bCreate = TRUE;
	}

	WdfWaitLockRelease(DeviceCollectionLock);

	

	if (!bCreate) {
		//
		// Control device is already created. So return success.
		//
		return STATUS_SUCCESS;
	}

	KdPrint(("Creating Control Device\n"));

	//
	// In order to create a control device, we first need to allocate a
	// WDFDEVICE_INIT structure and set all properties.
	//
	pInit = WdfControlDeviceInitAllocate(
		WdfDeviceGetDriver(Device),
		&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R
	);

	if (pInit == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Error;
	}

	//
	// Set exclusive to false so that more than one app can talk to the
	// control device simultaneously.
	//
	WdfDeviceInitSetExclusive(pInit, FALSE);

	status = WdfDeviceInitAssignName(pInit, &ntDeviceName);

	if (!NT_SUCCESS(status)) {
		goto Error;
	}

	//
	// Specify the size of device context
	//
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&controlAttributes,	CONTROL_DEVICE_CONTEXT);

	status = WdfDeviceCreate(&pInit, &controlAttributes, &controlDevice);
	if (!NT_SUCCESS(status)) {
		goto Error;
	}

	//
	// Create a symbolic link for the control object so that usermode can open
	// the device.
	//
	status = WdfDeviceCreateSymbolicLink(controlDevice,
		&symbolicLinkName);

	if (!NT_SUCCESS(status)) {
		goto Error;
	}


	//
	// Create IO queue for control device	
	//
	status = StorTraceControlDeviceQueueInitialize(controlDevice);
	if (!NT_SUCCESS(status)) {
		goto Error;
	}

	//
	// Control devices must notify WDF when they are done initializing.   I/O is
	// rejected until this call is made.
	//
	WdfControlFinishInitializing(controlDevice);

	ControlDevice = controlDevice;

	return STATUS_SUCCESS;

Error:

	if (pInit != NULL) {
		WdfDeviceInitFree(pInit);
	}

	if (controlDevice != NULL) {
		//
		// Release the reference on the newly created object, since
		// we couldn't initialize it.
		//
		WdfObjectDelete(controlDevice);
		controlDevice = NULL;
	}

	return status; 
}
