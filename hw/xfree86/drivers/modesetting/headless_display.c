/*
 * Copyright © 2020 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include "driver.h"

static void
headless_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
}

static Bool
headless_crtc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
                             Rotation rotation, int x, int y)
{
    return TRUE;
}

static void
headless_crtc_destroy(xf86CrtcPtr crtc)
{
}

static const xf86CrtcFuncsRec headless_crtc_funcs = {
    .dpms = headless_crtc_dpms,
    .set_mode_major = headless_crtc_set_mode_major,
    .destroy = headless_crtc_destroy,
};

static void
headless_output_create_resources(xf86OutputPtr output)
{
}

static Bool
headless_output_set_property(xf86OutputPtr output, Atom property,
                             RRPropertyValuePtr value)
{
    return TRUE;
}

static Bool
headless_output_get_property(xf86OutputPtr output, Atom property)
{
    return TRUE;
}

static void
headless_output_dpms(xf86OutputPtr output, int mode)
{
}

static xf86OutputStatus
headless_output_detect(xf86OutputPtr output)
{
        return XF86OutputStatusConnected;
}

static Bool
headless_output_mode_valid(xf86OutputPtr output, DisplayModePtr pModes)
{
    return MODE_OK;
}

static DisplayModePtr
headless_output_get_modes(xf86OutputPtr output)
{
    return NULL;
}

static void
headless_output_destroy(xf86OutputPtr output)
{
}

static const xf86OutputFuncsRec headless_output_funcs = {
    .dpms = headless_output_dpms,
    .create_resources = headless_output_create_resources,
    .set_property = headless_output_set_property,
    .get_property = headless_output_get_property,
    .detect = headless_output_detect,
    .mode_valid = headless_output_mode_valid,

    .get_modes = headless_output_get_modes,
    .destroy = headless_output_destroy
};

static Bool
headless_xf86crtc_resize(ScrnInfoPtr scrn, int width, int height)
{
    return TRUE;
}

static const xf86CrtcConfigFuncsRec headless_xf86crtc_config_funcs = {
    .resize = headless_xf86crtc_resize,
};

Bool
headless_output_init(ScrnInfoPtr pScrn)
{
    xf86OutputPtr output;
    char name[32];

    xf86CrtcConfigInit(pScrn, &headless_xf86crtc_config_funcs);

    snprintf(name, 32, "%s", "HEADLESS");
    output = xf86OutputCreate(pScrn, &headless_output_funcs, name);
    if (!output)
        return FALSE;

    output->mm_width = 0;
    output->mm_height = 0;
    output->subpixel_order = SubPixelNone;
    output->interlaceAllowed = FALSE;
    output->doubleScanAllowed = FALSE;
    output->status = XF86OutputStatusConnected;

    output->possible_crtcs = 0xFF;
    output->possible_clones = 0;

    return TRUE;
}

Bool
headless_crtc_init(ScrnInfoPtr pScrn)
{
    xf86CrtcPtr crtc;

    xf86CrtcSetSizeRange(pScrn, 320, 200, 7680, 4320);

    crtc = xf86CrtcCreate(pScrn, &headless_crtc_funcs);
    if (!crtc)
        return FALSE;

    return TRUE;
}

