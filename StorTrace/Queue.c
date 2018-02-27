/*++

Module Name:

    queue.c

Abstract:

    This file contains the queue entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "queue.tmh"

#include "srb.h"
#include "ntstrsafe.h"

#include "RingBuf.h"


//-------------------------------------------------------
// Function Decldaration
//-------------------------------------------------------
VOID
ForwardRequest(
	IN WDFREQUEST Request,
	IN WDFIOTARGET Target
);

VOID
ForwardRequestWithCompletion(
	IN WDFREQUEST Request,
	IN WDFIOTARGET Target
);

VOID
RequestCompletionRoutine(
	IN WDFREQUEST                  Request,
	IN WDFIOTARGET                 Target,
	PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
	IN WDFCONTEXT                  Context
);

VOID
ControlDeviceEvtIoDeviceControl(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t OutputBufferLength,
	_In_ size_t InputBufferLength,
	_In_ ULONG IoControlCode
);


//-------------------------------------------------------
// Imported Function & Variable Declaration
//-------------------------------------------------------
extern WDFCOLLECTION   DeviceCollection;
extern WDFWAITLOCK     DeviceCollectionLock;


//-------------------------------------------------------
// Function Implementation, For filter device queue
//-------------------------------------------------------
NTSTATUS
StorTraceQueueInitialize(
    _In_ WDFDEVICE Device
    )
{
    WDFQUEUE queue;
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
	

    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
         &queueConfig,
        WdfIoQueueDispatchParallel
        );

	// we need to handle IRP_MJ_INTERNAL_DEVICE_CONTROL which is also IRP_MJ_SCSI
	// queueConfig.EvtIoInternalDeviceControl = ....
    queueConfig.EvtIoDeviceControl = StorTraceEvtIoDeviceControl;
    queueConfig.EvtIoStop = StorTraceEvtIoStop;
	queueConfig.EvtIoInternalDeviceControl = StorTraceEvtIoInternalDeviceControl;
	queueConfig.EvtIoRead = StorTraceEvtIoRead;
	queueConfig.EvtIoWrite = StorTraceEvtIoWrite;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &queue
                 );
	 
    if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        return status;
    }

    return status;
}

VOID
StorTraceEvtIoRead(
	_In_ 	WDFQUEUE Queue,
	_In_	WDFREQUEST Request,
	_In_	size_t Length
)
{
	WDFDEVICE                       device;

	device = WdfIoQueueGetDevice(Queue);
	DbgPrint("%s, length 0x%x", __FUNCTION__, Length);

	ForwardRequest(Request, WdfDeviceGetIoTarget(device));

	return;
}

VOID
StorTraceEvtIoWrite(
	_In_	WDFQUEUE Queue,
	_In_	WDFREQUEST Request,
	_In_	size_t Length
)
{
	WDFDEVICE                       device;

	device = WdfIoQueueGetDevice(Queue);
	DbgPrint("%s, length 0x%x", __FUNCTION__, Length);

	ForwardRequest(Request, WdfDeviceGetIoTarget(device));

	return;
}


VOID
StorTraceEvtIoInternalDeviceControl(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t OutputBufferLength,
	_In_ size_t InputBufferLength,
	_In_ ULONG IoControlCode
)
{
	WDFDEVICE                       device;
	

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(IoControlCode);

	device = WdfIoQueueGetDevice(Queue);

	do
	{
		PIO_STACK_LOCATION  irpStack;
		

		// https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storage-filter-driver-s-dispatch-routines
		irpStack = IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(Request));
		if (irpStack == NULL)
		{
			DbgPrint("irpStack is null\n");
			break;
		}

		// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/wdm/ns-wdm-_io_stack_location
		// DbgPrint("Major 0x%x minor 0x%x, ", irpStack->MajorFunction, irpStack->MinorFunction);

		if (irpStack->MajorFunction != IRP_MJ_SCSI) {
			DbgPrint("\n");
			break;
		}

		PUCHAR              cdb;
		UCHAR               cdbLength;
		PSCSI_REQUEST_BLOCK srb;

		srb = irpStack->Parameters.Scsi.Srb;
		if (srb == NULL)
		{
			DbgPrint("srb is null\n");
			break;
		}

		cdb = srb->Cdb;
		if (cdb == NULL)
		{
			DbgPrint("cdb is null\n");
			break;
		}

		cdbLength = srb->CdbLength;
		if (cdbLength == 0)
		{
			break;
		}

		
		char dbgBuffer[100];
		char *pBufferPos = dbgBuffer;
		DbgPrint("CDB %2d bytes:", cdbLength);
		for (int i = 0; i < cdbLength; i++)
		{
			RtlStringCchPrintfA(pBufferPos, 3 + 1, " %2x", cdb[i]);
			pBufferPos += 3;
		}
		pBufferPos[0] = 0;
		DbgPrint("%s request 0x%p", dbgBuffer, Request);
		

		// TODO: make the copy faster, not one byte by one byte
		// Save data to CDB info to ring buffer
		
		RingBufPut(0x20); // magic number to 
		RingBufPut(0x18);
		RingBufPut(cdbLength);
		for (int i = 0; i < cdbLength; i++)
		{
			RingBufPut(cdb[i]);
		}
		
	} while (FALSE);
	
	ForwardRequestWithCompletion(Request, WdfDeviceGetIoTarget(device));

	return;
}

VOID
StorTraceEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
/*++

Routine Description:

    This event is invoked when the framework receives IRP_MJ_DEVICE_CONTROL request.

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    OutputBufferLength - Size of the output buffer in bytes

    InputBufferLength - Size of the input buffer in bytes

    IoControlCode - I/O control code.

Return Value:

    VOID

--*/
{
	WDFDEVICE                       device;
	PDEVICE_CONTEXT                 context;
	NTSTATUS                        status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d", 
                Queue, Request, (int) OutputBufferLength, (int) InputBufferLength, IoControlCode);

	DbgPrint("%s  ", __FUNCTION__);

	device = WdfIoQueueGetDevice(Queue);

	context = DeviceGetContext(device);

	switch (IoControlCode) {

		//
		// Put your cases for handling IOCTLs here
		//
	}

	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
		return;
	}

	//
	// Forward the request down. WdfDeviceGetIoTarget returns
	// the default target, which represents the device attached to us below in
	// the stack.
	//
	ForwardRequest(Request, WdfDeviceGetIoTarget(device));

	// WdfRequestComplete(Request, STATUS_SUCCESS);

    return;
}

VOID
StorTraceEvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags
)
/*++

Routine Description:

    This event is invoked for a power-managed queue before the device leaves the working state (D0).

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    ActionFlags - A bitwise OR of one or more WDF_REQUEST_STOP_ACTION_FLAGS-typed flags
                  that identify the reason that the callback function is being called
                  and whether the request is cancelable.

Return Value:

    VOID

--*/
{
    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p ActionFlags %d", 
                Queue, Request, ActionFlags);

    //
    // In most cases, the EvtIoStop callback function completes, cancels, or postpones
    // further processing of the I/O request.
    //
    // Typically, the driver uses the following rules:
    //
    // - If the driver owns the I/O request, it calls WdfRequestUnmarkCancelable
    //   (if the request is cancelable) and either calls WdfRequestStopAcknowledge
    //   with a Requeue value of TRUE, or it calls WdfRequestComplete with a
    //   completion status value of STATUS_SUCCESS or STATUS_CANCELLED.
    //
    //   Before it can call these methods safely, the driver must make sure that
    //   its implementation of EvtIoStop has exclusive access to the request.
    //
    //   In order to do that, the driver must synchronize access to the request
    //   to prevent other threads from manipulating the request concurrently.
    //   The synchronization method you choose will depend on your driver's design.
    //
    //   For example, if the request is held in a shared context, the EvtIoStop callback
    //   might acquire an internal driver lock, take the request from the shared context,
    //   and then release the lock. At this point, the EvtIoStop callback owns the request
    //   and can safely complete or requeue the request.
    //
    // - If the driver has forwarded the I/O request to an I/O target, it either calls
    //   WdfRequestCancelSentRequest to attempt to cancel the request, or it postpones
    //   further processing of the request and calls WdfRequestStopAcknowledge with
    //   a Requeue value of FALSE.
    //
    // A driver might choose to take no action in EvtIoStop for requests that are
    // guaranteed to complete in a small amount of time.
    //
    // In this case, the framework waits until the specified request is complete
    // before moving the device (or system) to a lower power state or removing the device.
    // Potentially, this inaction can prevent a system from entering its hibernation state
    // or another low system power state. In extreme cases, it can cause the system
    // to crash with bugcheck code 9F.
    //

    return;
}


VOID
ForwardRequest(
	IN WDFREQUEST Request,
	IN WDFIOTARGET Target
)
{
	WDF_REQUEST_SEND_OPTIONS options;
	BOOLEAN ret;
	NTSTATUS status;

	//
	// We are not interested in post processing the IRP so 
	// fire and forget.
	//
	WDF_REQUEST_SEND_OPTIONS_INIT(&options,
		WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

	ret = WdfRequestSend(Request, Target, &options);	

	if (ret == FALSE) {
		status = WdfRequestGetStatus(Request);
		KdPrint(("WdfRequestSend failed: 0x%x\n", status));
		WdfRequestComplete(Request, status);
	}

	return;
}

VOID
ForwardRequestWithCompletion(
	IN WDFREQUEST Request,
	IN WDFIOTARGET Target
)
{
	BOOLEAN ret;
	NTSTATUS status;

	//
	// The following funciton essentially copies the content of
	// current stack location of the underlying IRP to the next one. 
	//
	WdfRequestFormatRequestUsingCurrentType(Request);

	WdfRequestSetCompletionRoutine(Request,
		RequestCompletionRoutine,
		WDF_NO_CONTEXT);

	ret = WdfRequestSend(Request,
		Target,
		WDF_NO_SEND_OPTIONS);

	if (ret == FALSE) {
		status = WdfRequestGetStatus(Request);
		KdPrint(("WdfRequestSend failed: 0x%x\n", status));
		WdfRequestComplete(Request, status);
	}

	return;
}

VOID
RequestCompletionRoutine(
	IN WDFREQUEST                  Request,
	IN WDFIOTARGET                 Target,
	PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
	IN WDFCONTEXT                  Context
)
{
	UNREFERENCED_PARAMETER(Target);
	UNREFERENCED_PARAMETER(Context);

	WdfRequestComplete(Request, CompletionParams->IoStatus.Status);

	DbgPrint("%s request 0X%p\n", __FUNCTION__, Request);

	return;
}


//-------------------------------------------------------
// Function Implementation, For control device queue
//-------------------------------------------------------
NTSTATUS
StorTraceControlDeviceQueueInitialize(
	_In_ WDFDEVICE Device
)
{
	WDFQUEUE queue;
	NTSTATUS status;
	WDF_IO_QUEUE_CONFIG queueConfig;
	WDF_OBJECT_ATTRIBUTES  queueAttributes;

	//
	// Configure the default queue associated with the control device object
	// to be Serial so that request passed to EvtIoDeviceControl are serialized.
	//
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
		WdfIoQueueDispatchSequential);

	queueConfig.EvtIoDeviceControl = ControlDeviceEvtIoDeviceControl;
	queueConfig.EvtIoRead = ControlDeviceEvtIoRead;
	queueConfig.EvtIoWrite = ControlDeviceEvtIoWrite;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttributes, QUEUE_CONTEXT);
	queueAttributes.SynchronizationScope = WdfSynchronizationScopeQueue;

	//
	// Framework by default creates non-power managed queues for
	// filter drivers.
	//
	status = WdfIoQueueCreate(Device,
		&queueConfig,
		&queueAttributes,
		&queue // pointer to default queue
	);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to create Io Queue for controldevice");
		return status;
	}

	return status;
}


VOID
ControlDeviceEvtIoDeviceControl(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t OutputBufferLength,
	_In_ size_t InputBufferLength,
	_In_ ULONG IoControlCode
)
{
	
	ULONG               i;
	ULONG               noItems;
	WDFDEVICE           hDevice;
	PDEVICE_CONTEXT     deviceContext;
	
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(IoControlCode);

	
	// DbgPrint("%s.\n", __FUNCTION__);

	WdfWaitLockAcquire(DeviceCollectionLock, NULL);

	noItems = WdfCollectionGetCount(DeviceCollection);

	for (i = 0; i<noItems; i++) {

		hDevice = WdfCollectionGetItem(DeviceCollection, i);

		deviceContext = DeviceGetContext(hDevice);

		DbgPrint("Device Serial No: 0x%x\n", deviceContext->SerialNo);
	}

	WdfWaitLockRelease(DeviceCollectionLock);
	

	WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
}

VOID
ControlDeviceEvtIoWrite(
	_In_ 	WDFQUEUE Queue,
	_In_	WDFREQUEST Request,
	_In_	size_t Length
)
{
	WDFDEVICE                       device;
	NTSTATUS       status = STATUS_SUCCESS;

	device = WdfIoQueueGetDevice(Queue);
	DbgPrint("%s, length 0x%x", __FUNCTION__, Length);

	Length = 0L;
	WdfRequestSetInformation(Request, (ULONG_PTR)0);	

	WdfRequestComplete(Request, status);
	return;
}

VOID
ControlDeviceEvtIoRead(
	_In_ 	WDFQUEUE Queue,
	_In_	WDFREQUEST Request,
	_In_	size_t Length
)
{
	WDFDEVICE device;
	NTSTATUS status = STATUS_SUCCESS;
	WDFMEMORY memory;

	device = WdfIoQueueGetDevice(Queue);
	// DbgPrint("%s, length 0x%x", __FUNCTION__, Length);
	
	status = WdfRequestRetrieveOutputMemory(Request, &memory);

	if (!NT_SUCCESS(status)) {
		KdPrint(("EchoEvtIoRead Could not get request memory buffer 0x%x\n", status));
		WdfVerifierDbgBreakPoint();
		WdfRequestCompleteWithInformation(Request, status, 0L);
		return;
	}

	// Copy data byte by byte
	// TODO: optimize it
	size_t i;
	for (i = 0; i < Length; i++) {
		UCHAR data; 
		if (!RingBufGet(&data)) {
			break;
		}
		// Copy the memory out
		status = WdfMemoryCopyFromBuffer(
			memory,     // destination
			i,          // offset into the destination memory
			&data,      // source
			1);         // copy one byte
		if (!NT_SUCCESS(status)) {
			KdPrint(("EchoEvtIoRead: WdfMemoryCopyFromBuffer failed 0x%x\n", status));

			return;
		}
	}

	// Set how many bytes are copied
	WdfRequestSetInformation(Request, (ULONG_PTR)(i+1));
	
	WdfRequestComplete(Request, status);

	return;
}