/*
 * Copyright © 2013-2014 Intel Corporation
 * Copyright © 2015 Advanced Micro Devices, Inc.
 * Copyright © 2024 Yusuf Khan
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <scrnintstr.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dri3.h"
#include "driver.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <gbm.h>
#include <errno.h>
#include <libgen.h>

static int open_card_node(ClientPtr client,
                          ScreenPtr screen,
                          RRProviderPtr provider,
                          int *out)

{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	modesettingPtr ms = modesettingPTR(scrn);
	drm_magic_t magic;
	int fd;

	fd = open(drmGetDeviceNameFromFd(ms->fd), O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return BadAlloc;

	/* Before FD passing in the X protocol with DRI3 (and increased
	 * security of rendering with per-process address spaces on the
	 * GPU), the kernel had to come up with a way to have the server
	 * decide which clients got to access the GPU, which was done by
	 * each client getting a unique (magic) number from the kernel,
	 * passing it to the server, and the server then telling the
	 * kernel which clients were authenticated for using the device.
	 *
	 * Now that we have FD passing, the server can just set up the
	 * authentication on its own and hand the prepared FD off to the
	 * client.
	 */
	if (drmGetMagic(fd, &magic) < 0) {
		if (errno == EACCES) {
			/* Assume that we're on a render node, and the fd is
			 * already as authenticated as it should be.
			 */
			*out = fd;
			return Success;
		} else {
			close(fd);
			return BadMatch;
		}
	}

	if (drmAuthMagic(ms->fd, magic) < 0) {
		close(fd);
		return BadMatch;
	}

	*out = fd;
	return Success;
}

static int open_render_node(ScreenPtr screen, int *out)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	modesettingPtr ms = modesettingPTR(scrn);
	int fd;

	fd = open(ms->render_node, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return BadAlloc;

	*out = fd;
	return Success;
}

static int
ms_dri3_open(ScreenPtr screen, RRProviderPtr provider, int *out)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	modesettingPtr ms = modesettingPTR(scrn);
	int ret = BadAlloc;

	if (ms->render_node)
		ret = open_render_node(screen, out);

	/* These NULL bits are due to using open_card_node as a client wrapper*/
	if (ret != Success)
		ret = open_card_node(NULL, screen, NULL, out);

	return ret;
}

static dri3_screen_info_rec modesetting_dri3_screen_info = {
	.version = 2,
	.open = ms_dri3_open,
	.open_client = open_card_node,
	.pixmap_from_fds = glamor_pixmap_from_fds,
    .fds_from_pixmap = glamor_fds_from_pixmap,
    .get_formats = glamor_get_formats,
    .get_modifiers = glamor_get_modifiers,
    .get_drawable_modifiers = glamor_get_drawable_modifiers,
};

Bool
ms_dri3_screen_init(ScreenPtr screen)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    ms->render_node = drmGetRenderDeviceNameFromFd(ms->fd);

	if (!dri3_screen_init(screen, &modesetting_dri3_screen_info)) {
		xf86DrvMsg(scrn->scrnIndex, X_WARNING,
			   "dri3_screen_init failed\n");
		return FALSE;
	}

	return TRUE;
}

