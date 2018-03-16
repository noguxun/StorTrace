#pragma once

VOID
RingBufReset(void);

BOOLEAN
RingBufIsEmpty(void);

BOOLEAN
RingBufIsFull(void);

VOID
RingBufPut(UCHAR Data);

BOOLEAN
RingBufGet(UCHAR *pData);

VOID
RingBufPutEx(PUCHAR Data, UINT32 DataLength);
