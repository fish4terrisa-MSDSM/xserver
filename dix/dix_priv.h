/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2024 Enrico Weigelt, metux IT consult <info@metux.net>
 */
#ifndef _XSERVER_DIX_PRIV_H
#define _XSERVER_DIX_PRIV_H

/* This file holds global DIX settings to be used inside the Xserver,
 *  but NOT supposed to be accessed directly by external server modules like
 *  drivers or extension modules. Thus the definitions here are not part of the
 *  Xserver's module API/ABI.
 */

#include <X11/Xdefs.h>
#include <X11/Xfuncproto.h>

#include "include/dix.h"

/* server setting: maximum size for big requests */
#define MAX_BIG_REQUEST_SIZE 4194303
extern long maxBigRequestSize;

extern char dispatchExceptionAtReset;
extern int terminateDelay;
extern Bool touchEmulatePointer;

extern HWEventQueuePtr checkForInput[2];

static inline _X_NOTSAN Bool
InputCheckPending(void)
{
    return (*checkForInput[0] != *checkForInput[1]);
}

void ClearWorkQueue(void);
void ProcessWorkQueue(void);
void ProcessWorkQueueZombies(void);

void CloseDownClient(ClientPtr client);

#endif /* _XSERVER_DIX_PRIV_H */
