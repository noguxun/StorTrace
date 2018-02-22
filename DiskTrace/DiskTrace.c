#include "ntddk.h"
#include "ntdddisk.h"
#include "ntdef.h"

//
// Device Extension
//



typedef struct _DEVICE_EXTENSION {
	// Back pointer to device object
	PDEVICE_OBJECT DeviceObject;

	// Target Device Object	
	PDEVICE_OBJECT TargetDeviceObject;

	// Physical device object	
	PDEVICE_OBJECT PhysicalDeviceObject;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

#define DEVICE_EXTENSION_SIZE sizeof(DEVICE_EXTENSION)





NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath);
NTSTATUS DiskTraceAddDevice(IN PDRIVER_OBJECT DriverObject, IN PDEVICE_OBJECT PhysicalDeviceObject);
VOID DiskTraceUnload(IN PDRIVER_OBJECT DriverObject);

#ifdef ALLOC_PRAGMA
// #pragma alloc_text (INIT, DriverEntry)
// #pragma alloc_text (PAGE, DiskTraceAddDevice)
// #pragma alloc_text (PAGE, DiskTraceUnload)
#endif




NTSTATUS
DiskTraceSendToNextDriver(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp
)
{
	PDEVICE_EXTENSION   deviceExtension;

	IoSkipCurrentIrpStackLocation(Irp);
	deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

	DbgPrint("DiskTraceSendToNextDriver: Device extension is null!\n");
	if (deviceExtension == NULL)
	{
		DbgPrint("DiskTraceSendToNextDriver: Device extension is null!\n");
		return STATUS_UNSUCCESSFUL;
	}

	return IoCallDriver(deviceExtension->TargetDeviceObject, Irp);
}

VOID
DiskTraceUnload(
	IN PDRIVER_OBJECT DriverObject
)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(DriverObject);

	// TODO: 
	// do we need to delete the list of device?
	/*    
	PDEVICE_OBJECT dev_obj = DriverObject->DeviceObject;
    
    if ( dev_obj )  // this is a list
        IoDeleteDevice(dev_obj); 
	*/

	return;
}

NTSTATUS
DiskTraceAddDevice(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT PhysicalDeviceObject
)
{
	NTSTATUS                status;
	PDEVICE_OBJECT          filterDeviceObject;
	PDEVICE_EXTENSION       deviceExtension;
	

	DbgPrint("Add Device");

	status = IoCreateDevice(DriverObject,
		DEVICE_EXTENSION_SIZE,
		NULL,
		FILE_DEVICE_DISK,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&filterDeviceObject);

	if (!NT_SUCCESS(status)) {
		DbgPrint("DiskPerfAddDevice: Cannot create filterDeviceObject\n");
		return status;
	}

	filterDeviceObject->Flags |= DO_DIRECT_IO;

	deviceExtension = (PDEVICE_EXTENSION)filterDeviceObject->DeviceExtension;
	RtlZeroMemory(deviceExtension, DEVICE_EXTENSION_SIZE);

	//
	// Attaches the device object to the highest device object in the chain and
	// return the previously highest device object, which is passed to
	// IoCallDriver when pass IRPs down the device stack
	//
	deviceExtension->PhysicalDeviceObject = PhysicalDeviceObject;

	// GU: Is this TargetDeviceObject the FDO of the lower level? 
	deviceExtension->TargetDeviceObject =
		IoAttachDeviceToDeviceStack(filterDeviceObject, PhysicalDeviceObject);

	if (deviceExtension->TargetDeviceObject == NULL) {			
		IoDeleteDevice(filterDeviceObject);
		DbgPrint("DiskPerfAddDevice: Unable to attach 0x%p to target 0x%p\n",
			filterDeviceObject, PhysicalDeviceObject);
		return STATUS_NO_SUCH_DEVICE;
	}

	// TODO: remove lock

	// Save the filter device object in the device extension
	deviceExtension->DeviceObject = filterDeviceObject;

	// 
	// I do not want to make it to be pageable
	//   https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/making-drivers-pageable
	// filterDeviceObject->Flags |= DO_POWER_PAGABLE;


	// Clear the DO_DEVICE_INITIALIZING flag
	filterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;
}


NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
)
{
	ULONG               ulIndex;
	PDRIVER_DISPATCH  * dispatch;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	for (ulIndex = 0, dispatch = DriverObject->MajorFunction;
		ulIndex <= IRP_MJ_MAXIMUM_FUNCTION;
		ulIndex++, dispatch++) {

		*dispatch = DiskTraceSendToNextDriver;
	}

	
	// Required IO control
	//   https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storage-filter-driver-s-support-of-i-o-requests
    //   https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storage-class-driver-s-support-of-i-o-requests
	/*
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_READ] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_PNP] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_POWER] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = DiskTraceSendToNextDriver;

	DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = DiskTraceSendToNextDriver;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DiskTraceSendToNextDriver;
	*/

	DriverObject->DriverExtension->AddDevice = DiskTraceAddDevice;
	DriverObject->DriverUnload = DiskTraceUnload;

	return(STATUS_SUCCESS);

} // end DriverEntry()



