/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_StorTrace,
    0xd0483345,0x7c86,0x45b6,0xb9,0x48,0x28,0x14,0xf9,0x5f,0x72,0x36);
// {d0483345-7c86-45b6-b948-2814f95f7236}
