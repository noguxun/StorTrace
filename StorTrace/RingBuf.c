/*
    Reference:
    https://embeddedartistry.com/blog/2017/4/6/circular-buffers-in-cc
*/

#include "driver.h"
#include "ntdef.h"

#include "RingBuf.h"


#define RING_BUF_SIZE  (100 * 1024)

typedef struct _RING_BUF {
    UCHAR  Buffer[RING_BUF_SIZE];
    size_t  Head;
    size_t  Tail;
    size_t  Size; //of the buffer
} RING_BUFF;

static RING_BUFF RingBuf;
static WDFWAITLOCK RingBufLock;

static void 
Lock(void)
{
    WdfWaitLockAcquire(RingBufLock, NULL);
}

static void 
Unlock(void)
{
    WdfWaitLockRelease(RingBufLock);
}

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
RingBufPut(UCHAR Data)
{    
    Lock();

    RingBuf.Buffer[RingBuf.Head] = Data;
    RingBuf.Head = (RingBuf.Head + 1) % RingBuf.Size;

    if (RingBuf.Head == RingBuf.Tail)
    {
        RingBuf.Tail = (RingBuf.Tail + 1) % RingBuf.Size;
    }

    Unlock();
}

BOOLEAN
RingBufGet(UCHAR *pData)
{
    BOOLEAN r = FALSE;

    Lock();

    if (pData && !RingBufIsEmpty())
    {
        *pData = RingBuf.Buffer[RingBuf.Tail];
        RingBuf.Tail = (RingBuf.Tail + 1) % RingBuf.Size;

        r = TRUE;
    }

    Unlock();

    return r;
}


