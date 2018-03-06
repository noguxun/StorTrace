/*++

Module Name:

    queue.h

Abstract:

    This file contains the queue definitions.

Environment:

    Kernel-mode Driver Framework

--*/

EXTERN_C_START

//
// This is the context that can be placed per queue
// and would contain per queue information.
//
typedef struct _QUEUE_CONTEXT {

    ULONG PrivateDeviceData;  // just a placeholder

} QUEUE_CONTEXT, *PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, QueueGetContext)

NTSTATUS
StorTraceQueueInitialize(
    _In_ WDFDEVICE Device
);

NTSTATUS
StorTraceControlDeviceQueueInitialize(
    _In_ WDFDEVICE Device
);

//
// Events from the IoQueue object
//
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL StorTraceEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP StorTraceEvtIoStop;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL StorTraceEvtIoInternalDeviceControl;
EVT_WDF_IO_QUEUE_IO_WRITE StorTraceEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_READ StorTraceEvtIoRead;

EVT_WDF_IO_QUEUE_IO_WRITE ControlDeviceEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_READ ControlDeviceEvtIoRead;

EXTERN_C_END
