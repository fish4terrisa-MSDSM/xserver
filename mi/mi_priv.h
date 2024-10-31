/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2024 Enrico Weigelt, metux IT consult <info@metux.net>
 */
#ifndef _XSERVER_MI_PRIV_H
#define _XSERVER_MI_PRIV_H

#include <X11/Xprotostr.h>

#include "include/gc.h"
#include "include/pixmap.h"
#include "include/screenint.h"
#include "mi/mi.h"

void miScreenClose(ScreenPtr pScreen);

void miWideArc(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs);

#endif /* _XSERVER_MI_PRIV_H */
