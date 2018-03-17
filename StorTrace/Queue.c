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
#include "ntddscsi.h"
#include "scsi.h"

#include "RingBuf.h"

//-------------------------------------------------------
// Macro
//-------------------------------------------------------
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

//-------------------------------------------------------
// Function Decldaration
//-------------------------------------------------------
static VOID
ForwardRequest(
    IN WDFREQUEST Request,
    IN WDFIOTARGET Target
);

static VOID
ForwardRequestWithCompletion(
    IN WDFREQUEST Request,
    IN WDFIOTARGET Target,
    IN PFN_WDF_REQUEST_COMPLETION_ROUTINE CompletionFunc
);

static VOID
CompletionIoCtlScsiPassThrDirect(
    IN WDFREQUEST                  Request,
    IN WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    IN WDFCONTEXT                  Context
);

static VOID
ControlDeviceEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
);

static VOID
DbgPrintCdb(_In_ PUCHAR pCdb, _In_ UCHAR CdbLength);

static VOID
SaveCdbToRingBufEx(_In_ PUCHAR pCdb, _In_ UCHAR CdbLength, _In_ PUCHAR SenseData, _In_ UCHAR SenseDataLength, _In_ NTSTATUS status);

static VOID
SaveCdbToRingBuf(_In_ PUCHAR pCdb, _In_ UCHAR CdbLength);

static BOOLEAN
GetByteFromRingBuf(_Out_ PUCHAR Data);
//-------------------------------------------------------
// Imported Function & Variable Declaration
//-------------------------------------------------------
extern WDFCOLLECTION   DeviceCollection;
extern WDFWAITLOCK     DeviceCollectionLock;
extern WDFSPINLOCK     CdbBufSpinLock;


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
    _In_    WDFQUEUE Queue,
    _In_    WDFREQUEST Request,
    _In_    size_t Length
)
{
    WDFDEVICE                       device;

    device = WdfIoQueueGetDevice(Queue);
    DbgPrint("%s, length 0x%x \n", __FUNCTION__, (int)Length);

    ForwardRequest(Request, WdfDeviceGetIoTarget(device));

    return;
}

VOID
StorTraceEvtIoWrite(
    _In_    WDFQUEUE Queue,
    _In_    WDFREQUEST Request,
    _In_    size_t Length
)
{
    WDFDEVICE                       device;

    device = WdfIoQueueGetDevice(Queue);
    DbgPrint("%s, length 0x%x \n", __FUNCTION__, (int)Length);

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

    DbgPrint("%s \n", __FUNCTION__);

    
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

        //
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/wdm/ns-wdm-_io_stack_location
        // 
        if (irpStack->MajorFunction != IRP_MJ_SCSI) {
            DbgPrint("%s Major 0x%x minor 0x%x \n", __FUNCTION__, irpStack->MajorFunction, irpStack->MinorFunction);
            break;
        }


        PSCSI_REQUEST_BLOCK srb;
        PUCHAR cdb;
        UCHAR cdbLength;

        srb = irpStack->Parameters.Scsi.Srb;
        if (srb == NULL)
        {
            DbgPrint("srb is null\n");
            break;
        }

        // Could be STORAGE_REQUEST_BLOCK, checking the function field
        // https://www.osr.com/nt-insider/2014-issue4/win7-vs-win8-storport-miniports/
        //
        if (srb->Function == SRB_FUNCTION_EXECUTE_SCSI)
        {                  
            cdb = srb->Cdb;
            cdbLength = srb->CdbLength;

            if (cdbLength == 0 || cdbLength > 16)
            {
                DbgPrint("CDB %2d bytes, abnormal!!\n", cdbLength);
                break;
            }

            SaveCdbToRingBuf(cdb, cdbLength);
        }
        else if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
        {
            PSTORAGE_REQUEST_BLOCK  storRequestBlock = (PSTORAGE_REQUEST_BLOCK)srb;            

            DbgPrint("NumSrbExData %d \n", storRequestBlock->NumSrbExData);

            for (ULONG srbExDataIndex = 0; srbExDataIndex < storRequestBlock->NumSrbExData; srbExDataIndex++)
            {

                PSRBEX_DATA srbExDataTmp = (PSRBEX_DATA)((PUCHAR)storRequestBlock + storRequestBlock->SrbExDataOffset[srbExDataIndex]);
                DbgPrint("SrbExType %x \n", srbExDataTmp->Type);

                if (srbExDataTmp->Type == SrbExDataTypeScsiCdb16) 
                {
                    PSRBEX_DATA_SCSI_CDB16 srbExCdb16 = (PSRBEX_DATA_SCSI_CDB16)srbExDataTmp;

                    cdb = srbExCdb16->Cdb;
                    cdbLength = srbExCdb16->CdbLength;

                    if (cdbLength == 0 || cdbLength > 16)
                    {
                        DbgPrint("CDB %2d bytes, abnormal!!\n", cdbLength);
                        break;
                    }                    
                }
                else if (srbExDataTmp->Type == SrbExDataTypeScsiCdb32)
                {
                    PSRBEX_DATA_SCSI_CDB32 srbExCdb32 = (PSRBEX_DATA_SCSI_CDB32)srbExDataTmp;

                    cdb = srbExCdb32->Cdb;
                    cdbLength = srbExCdb32->CdbLength;

                    if (cdbLength == 0 || cdbLength > 32)
                    {
                        DbgPrint("CDB %2d bytes, abnormal!!\n", cdbLength);
                        break;
                    }                    
                }                
                else if (srbExDataTmp->Type == SrbExDataTypeScsiCdbVar)
                {
                    PSRBEX_DATA_SCSI_CDB_VAR srbExCdbVar = (PSRBEX_DATA_SCSI_CDB_VAR)srbExDataTmp;

                    cdb = srbExCdbVar->Cdb;
                    cdbLength = (srbExCdbVar->CdbLength > 255) ? 255 : (UCHAR)srbExCdbVar->CdbLength;                    

                    if (cdbLength == 0)
                    {
                        DbgPrint("CDB %2d bytes, abnormal!!\n", cdbLength);
                        break;
                    }                    
                }
                else {
                    continue;
                }

                SaveCdbToRingBuf(cdb, cdbLength);
            }        
        }
        else 
        {
            DbgPrint("srb function is 0x%x, not supported\n", srb->Function);
            break;
        }
    } while (FALSE);
        

    ForwardRequest(Request, WdfDeviceGetIoTarget(device));

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
    WDFDEVICE device;
    
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d", Queue, Request, (int)OutputBufferLength, (int)InputBufferLength, IoControlCode);

    device = WdfIoQueueGetDevice(Queue);


    if (IoControlCode != IOCTL_SCSI_PASS_THROUGH_DIRECT) {
        DbgPrint("IoControlCode 0x%x, not IOCTL_SCSI_PASS_THROUGH_DIRECT\n", IoControlCode);
        ForwardRequest(Request, WdfDeviceGetIoTarget(device));
    }
    else
    {
        ForwardRequestWithCompletion(Request, WdfDeviceGetIoTarget(device), CompletionIoCtlScsiPassThrDirect);
    }        

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
    IN WDFIOTARGET Target,
    PFN_WDF_REQUEST_COMPLETION_ROUTINE CompletionFunc
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
        CompletionFunc,
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
CompletionIoCtlScsiPassThrDirect(
    IN WDFREQUEST                  Request,
    IN WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    IN WDFCONTEXT                  Context
)
{
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Context);    

    // 
    // Storage class drivers set the minor IRP number to IRP_MN_SCSI_CLASS to indicate that the request has been processed by a storage class driver. 
    // Parameters.DeviceIoControl.InputBufferLength indicates the size, in bytes, of the buffer at Irp->AssociatedIrp.SystemBuffer, which must be at least 
    //  (sense data size + sizeof (SCSI_PASS_THROUGH_DIRECT)). The size of the SCSI_PASS_THROUGH_DIRECT structure is fixed.
    // For most public I / O control codes, device drivers transfer a small amount of data to or from the buffer at Irp->AssociatedIrp.SystemBuffer.
    // Reference:
    //   https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntddscsi/ni-ntddscsi-ioctl_scsi_pass_through_direct
    //   https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntddscsi/ns-ntddscsi-_scsi_pass_through_direct
    //   https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/irp-mj-device-control
    //   https://github.com/Microsoft/Windows-driver-samples/blob/master/filesys/fastfat/devctrl.c 
    //   https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/wdm/ns-wdm-_io_stack_location
    //   https://docs.microsoft.com/en-us/windows-hardware/drivers/wdf/wdm-equivalents-for-kmdf-buffer-pointers
    //
    do {
        PVOID  buffer;
        size_t  bufferSize;
        NTSTATUS status;
        size_t minSize;
        PUCHAR pCdb;
        UCHAR cdbLength;
        PUCHAR senseData = NULL;
        UCHAR senseLength = 0;
        
        PIRP irp = WdfRequestWdmGetIrp(Request);
        PIO_STACK_LOCATION  irpStack = IoGetCurrentIrpStackLocation(irp);
        if (irpStack->MajorFunction != IRP_MJ_DEVICE_CONTROL)
        {
            DbgPrint("Not IRP_MJ_DEVICE_CONTROL, type is %d\n", irpStack->MajorFunction);
            break;
        }

        if (WdfRequestIsFrom32BitProcess(Request)) {
            minSize = sizeof(SCSI_PASS_THROUGH_DIRECT32);
        }
        else {
            minSize = sizeof(SCSI_PASS_THROUGH_DIRECT);
        }

        status = WdfRequestRetrieveInputBuffer(Request, minSize, &buffer, &bufferSize);
        if (!NT_SUCCESS(status)) {
            DbgPrint("Cannot get the input buffer\n");
            break;
        }

        if (WdfRequestIsFrom32BitProcess(Request)) {
            PSCSI_PASS_THROUGH_DIRECT32 pScsi = buffer;
            pCdb = pScsi->Cdb;
            cdbLength = pScsi->CdbLength;
            if (pScsi->SenseInfoLength)
            {
                senseData = ((PUCHAR)pScsi + pScsi->SenseInfoOffset);
                senseLength = pScsi->SenseInfoLength;
            }

            DbgPrint("senseLen %d, offsetset %d\n", pScsi->SenseInfoLength, pScsi->SenseInfoOffset);
        }
        else {
            PSCSI_PASS_THROUGH_DIRECT pScsi = buffer;
            pCdb = pScsi->Cdb;
            cdbLength = pScsi->CdbLength;
            if (pScsi->SenseInfoLength)
            {
                senseData = ((PUCHAR)pScsi + pScsi->SenseInfoOffset);
                senseLength = pScsi->SenseInfoLength;
            }
            DbgPrint("senseLen %d, offsetset %d\n", pScsi->SenseInfoLength, pScsi->SenseInfoOffset);            
        }

        if (cdbLength == 0 || cdbLength > 16)
        {
            DbgPrint("CDB %2d bytes, abnormal!!\n", cdbLength);
            break;
        }

        //
        // Save CDB to ring buf
        //
        SaveCdbToRingBufEx(pCdb, cdbLength, senseData, senseLength, CompletionParams->IoStatus.Status);

    } while (FALSE);

    WdfRequestComplete(Request, CompletionParams->IoStatus.Status);

    return;
}

VOID 
DbgPrintCdb(PUCHAR pCdb, UCHAR CdbLength)
{
    char dbgBuffer[100];
    char *pBufferPos = dbgBuffer;
    for (UCHAR i = 0; i < CdbLength; i++)
    {
        RtlStringCchPrintfA(pBufferPos, 3 + 1, " %02x", pCdb[i]);
        pBufferPos += 3;
    }
    pBufferPos[0] = 0;
    DbgPrint("%s \n", dbgBuffer);
}

BOOLEAN
GetByteFromRingBuf(PUCHAR Data)
{
    BOOLEAN success;
    WdfSpinLockAcquire(CdbBufSpinLock);

    success = RingBufGet(Data);

    WdfSpinLockRelease(CdbBufSpinLock);

    return success;
}

VOID
SaveCdbToRingBuf(PUCHAR Cdb, UCHAR CdbLength)
{
    SaveCdbToRingBufEx(Cdb, CdbLength, NULL, 0, 0);
}

VOID 
SaveCdbToRingBufEx(PUCHAR Cdb, UCHAR CdbLength, PUCHAR SenseData, UCHAR SenseDataLength, NTSTATUS status)
{
    WdfSpinLockAcquire(CdbBufSpinLock);

    DbgPrintCdb(Cdb, CdbLength);

    // TODO: make the copy faster, not one byte by one byte
    // Save data to CDB info to ring buffer

    // Magic number 0xDEAF
    RingBufPut(0xDE); 
    RingBufPut(0xAF);

    // Status code
    RingBufPut((UCHAR)status);
    RingBufPut((UCHAR)(status >> 8));
    RingBufPut((UCHAR)(status >> 16));
    RingBufPut((UCHAR)(status >> 24));
    
    // CDB length
    RingBufPut(CdbLength);

    // Sense Data length
    RingBufPut(SenseDataLength);

    // CDB data
    RingBufPutEx(Cdb, CdbLength);

    // Sense data (if any)
    if (SenseDataLength)
    {
        ASSERT(SenseData);

        // raw data
        RingBufPutEx(SenseData, SenseDataLength);
    }

    WdfSpinLockRelease(CdbBufSpinLock);
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
        DbgPrint("Failed to create Io Queue for controldevice\n");
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
    _In_     WDFQUEUE Queue,
    _In_    WDFREQUEST Request,
    _In_    size_t Length
)
{
    WDFDEVICE                       device;
    NTSTATUS       status = STATUS_SUCCESS;

    device = WdfIoQueueGetDevice(Queue);
    DbgPrint("%s, length 0x%x \n", __FUNCTION__, Length);

    Length = 0L;
    WdfRequestSetInformation(Request, (ULONG_PTR)0);    

    WdfRequestComplete(Request, status);
    return;
}

VOID
ControlDeviceEvtIoRead(
    _In_     WDFQUEUE Queue,
    _In_    WDFREQUEST Request,
    _In_    size_t Length
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
    size_t copied;
    for (copied = 0; copied < Length; copied++) {
        UCHAR data; 
        if (!GetByteFromRingBuf(&data)) {
            break;
        }
        // Copy the memory out
        status = WdfMemoryCopyFromBuffer(
            memory,     // destination
            copied,     // offset into the destination memory
            &data,      // source
            1);         // copy one byte
        if (!NT_SUCCESS(status)) {
            KdPrint(("EchoEvtIoRead: WdfMemoryCopyFromBuffer failed 0x%x\n", status));
            break;
        }
    }

    // 
    // Set how many bytes are copied
    WdfRequestSetInformation(Request, (ULONG_PTR)copied);
    
    WdfRequestComplete(Request, status);

    return;
}
