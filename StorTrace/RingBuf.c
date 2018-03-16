/*
    Reference:
    https://embeddedartistry.com/blog/2017/4/6/circular-buffers-in-cc
*/

#include "driver.h"
#include "ntdef.h"

#include "RingBuf.h"


//=========================================
// Macro definition
//=========================================
#define RING_BUF_SIZE  (10 * 1024)

//=========================================
// Data Type Definition
//=========================================

typedef struct _RING_BUF {
    UCHAR  Buffer[RING_BUF_SIZE];
    size_t  Head;
    size_t  Tail;
    size_t  Size; //of the buffer
} RING_BUFF;


//=========================================
// Function Declaration
//=========================================
static VOID
InternalPut(UCHAR Data);

//=========================================
// Variable definition
//=========================================
static RING_BUFF RingBuf;
static WDFWAITLOCK RingBufLock;


//=========================================
// Public Function
//=========================================

VOID
RingBufReset(void)
{
    RingBuf.Head = 0;
    RingBuf.Tail = 0;
    RingBuf.Size = RING_BUF_SIZE;

    NTSTATUS status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &RingBufLock);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("RingBufLock failed with status 0x%x\n", status);
    }
}

BOOLEAN
RingBufIsEmpty(void)
{    
    // We define empty as Head == Tail
    return (RingBuf.Head == RingBuf.Tail);
}

BOOLEAN
RingBufIsFull(void)
{
    // We determine "full" case by Head being one position behind the Tail
    // Note that this means we are wasting one space in the buffer!
    // Instead, you could have an "empty" flag and determine buffer full that way
    return ((RingBuf.Head + 1) % RingBuf.Size) == RingBuf.Tail;
}


VOID
RingBufPutEx(PUCHAR Data, UINT32 DataLength)
{    
    if (FALSE)
    //if ((RingBuf.Head + DataLength) < (RingBuf.Size - 1))
    {
        RtlCopyMemory(&(RingBuf.Buffer[RingBuf.Head]), Data, DataLength);
        // if the head will cross the tail, meaning overwrite some data
        if (RingBuf.Head < RingBuf.Tail && (RingBuf.Head + DataLength) >= RingBuf.Tail)
        {
            RingBuf.Tail = (RingBuf.Head + DataLength) + 1;
        }

        RingBuf.Head = (RingBuf.Head + DataLength);
    }
    else
    {
        for (UINT32 i = 0; i < DataLength; i++)
        {
            InternalPut(Data[i]);
        }
    }
}


VOID
RingBufPut(UCHAR Data)
{        
    InternalPut(Data);
}

BOOLEAN
RingBufGet(UCHAR *pData)
{
    BOOLEAN r = FALSE;

    if (pData && !RingBufIsEmpty())
    {
        *pData = RingBuf.Buffer[RingBuf.Tail];
        RingBuf.Tail = (RingBuf.Tail + 1) % RingBuf.Size;

        r = TRUE;
    }

    return r;
}


//=========================================
//  Private function
//=========================================
VOID
InternalPut(UCHAR Data)
{
    RingBuf.Buffer[RingBuf.Head] = Data;
    RingBuf.Head = (RingBuf.Head + 1) % RingBuf.Size;

    if (RingBuf.Head == RingBuf.Tail)
    {
        RingBuf.Tail = (RingBuf.Tail + 1) % RingBuf.Size;
    }
}
