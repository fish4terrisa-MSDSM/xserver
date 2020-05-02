/*
 * Copyright © 2020 Dario Nieuwenhuis
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the
 * copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef XWAYLAND_EXT_H
#define XWAYLAND_EXT_H

#include <xwayland-config.h>

#define XWAYLANDNAME "XWAYLAND"
#define XwaylandNumberEvents 0
#define XwaylandNumberErrors 0

#define XWAYLAND_MAJOR_VERSION	1	/* current version numbers */
#define XWAYLAND_MINOR_VERSION	0

#define X_XwaylandQueryVersion	0
#define X_XwaylandSetScale  	1


typedef struct _XwaylandQueryVersion {
    CARD8	reqType;		    /* always XwaylandReqCode */
    CARD8	xwaylandReqType;	/* always X_XwaylandQueryVersion */
    CARD16	length;
} xXwaylandQueryVersionReq;
#define sz_xXwaylandQueryVersionReq	4

typedef struct {
    BYTE	type;			    /* X_Reply */
    BOOL	pad1;
    CARD16	sequenceNumber;
    CARD32	length;
    CARD16	majorVersion;		/* major version of Xwayland */
    CARD16	minorVersion;		/* minor version of Xwayland */
    CARD32	pad2;
    CARD32	pad3;
    CARD32	pad4;
    CARD32	pad5;
    CARD32	pad6;
} xXwaylandQueryVersionReply;
#define sz_xXwaylandQueryVersionReply	32


typedef struct {
    CARD8	reqType;		    /* always XwaylandReqCode */
    CARD8	xwaylandReqType;	/* always X_XwaylandSetScale */
    CARD16	length;
    CARD16	screen;
    CARD16	scale;
} xXwaylandSetScaleReq;
#define sz_xXwaylandSetScaleReq	8

void xwlExtensionInit(void);

#endif /* XWAYLAND_EXT_H */
