/*
 * Copyright © 2020 Roman Gilg
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

#include <xwayland-config.h>

#include "cursorstr.h"

#include "xwayland-cursor-fake.h"
#include "xwayland-cursor.h"
#include "xwayland-input.h"
#include "xwayland-screen.h"
#include "xwayland-shm.h"
#include "xwayland-window.h"

#include <wayland-client.h>
#include "pointer-constraints-unstable-v1-client-protocol.h"

static void
xwl_cursor_create_fake_subsurface(struct xwl_seat *xwl_seat, struct xwl_window *parent);

void
xwl_cursor_set_fake_cursor(struct xwl_seat *xwl_seat)
{
    struct xwl_cursor *xwl_cursor = &xwl_seat->cursor;
    PixmapPtr pixmap;

    if (xwl_cursor->frame_cb) {
        xwl_cursor->needs_update = TRUE;
        return;
    }

    if (xwl_seat->wl_pointer)
        /* Hide the cursor of the native pointer. */
        wl_pointer_set_cursor(xwl_seat->wl_pointer,
                              xwl_seat->pointer_enter_serial, NULL, 0, 0);

    if (!xwl_seat->x_cursor)
        return;

    pixmap = xwl_cursor_get_pixmap(xwl_seat);
    if (!pixmap)
        return;

    xwl_cursor_send_surface(xwl_seat, xwl_cursor, pixmap, TRUE);
}

void
xwl_cursor_fake_update_focus(struct xwl_screen *xwl_screen, WindowPtr window)
{
    struct xwl_seat *xwl_seat = xwl_screen_get_default_seat(xwl_screen);
    struct xwl_window *xwl_window;

    if (!xwl_seat)
        return;

    xwl_window = xwl_window_from_window(window);

    if (xwl_window && xwl_seat->cursor.fake && xwl_seat->cursor.fake->parent != xwl_window) {
        xwl_cursor_create_fake_subsurface(xwl_seat, xwl_window);
        xwl_cursor_set_fake_cursor(xwl_seat);
    }
}

static void
xwl_cursor_destroy_fake_subsurface(struct xwl_seat *xwl_seat)
{
    struct xwl_cursor_fake *fake = xwl_seat->cursor.fake;

    wl_subsurface_destroy(fake->subsurface);
    wl_surface_destroy(fake->surface);
    fake->subsurface = NULL;
    fake->surface = NULL;
}

void
xwl_cursor_destroy_fake(struct xwl_seat *xwl_seat)
{
    if (!xwl_seat->cursor.fake)
        return;

    zwp_locked_pointer_v1_destroy(xwl_seat->cursor.fake->locked_pointer);
    xwl_cursor_destroy_fake_subsurface(xwl_seat);

    free(xwl_seat->cursor.fake);
    xwl_seat->cursor.fake = NULL;

    xwl_seat_set_cursor(xwl_seat);
}

static void
xwl_cursor_create_fake_subsurface(struct xwl_seat *xwl_seat, struct xwl_window *parent)
{
    struct xwl_cursor_fake *fake = xwl_seat->cursor.fake;

    if (fake->subsurface)
        xwl_cursor_destroy_fake_subsurface(xwl_seat);

    fake->parent = parent;

    fake->surface = wl_compositor_create_surface(xwl_seat->xwl_screen->compositor);
    fake->subsurface = wl_subcompositor_get_subsurface(xwl_seat->xwl_screen->subcompositor,
                                                       fake->surface,
                                                       parent->surface);

    wl_surface_set_input_region(fake->surface, NULL);
    wl_subsurface_set_desync(fake->subsurface);
}

static void
xwl_cursor_create_fake(struct xwl_seat *xwl_seat)
{
    struct xwl_cursor *xwl_cursor = &xwl_seat->cursor;

    if (xwl_cursor->fake)
        return;

    if (!xwl_seat->focus_window)
        /* We may only begin a fake when we have focus. */
        return;

    xwl_cursor->fake = calloc(1, sizeof *xwl_cursor->fake);

    xwl_seat->cursor.fake->origin = xwl_seat->focus_window;

    xwl_cursor->fake->locked_pointer =
        zwp_pointer_constraints_v1_lock_pointer(xwl_seat->xwl_screen->pointer_constraints,
                                                xwl_seat->cursor.fake->origin->surface,
                                                xwl_seat->wl_pointer,
                                                NULL,
                                                ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);

    xwl_cursor_create_fake_subsurface(xwl_seat, xwl_seat->cursor.fake->origin);
    xwl_cursor_set_fake_cursor(xwl_seat);
}

void
xwl_cursor_fake_position(struct xwl_seat *xwl_seat, int x, int y)
{
    int parent_x, parent_y, origin_x, origin_y;

    if (!xwl_seat)
        /* We could still try to fake the position with subsurfaces but we just require an active
         * seat for storing data. */
        return;

    if (xwl_seat->pointer_warp_emulator)
        /* Cursor locked for warp emulation. Do not try to fake the position in this case. */
        return;

    if (!xwl_seat->xwl_screen->pointer_constraints
            || !xwl_seat->xwl_screen->subcompositor)
        /* Faking the cursor needs support of pointer constraints and subsurfaces. */
        return;

    xwl_cursor_create_fake(xwl_seat);
    if (!xwl_seat->cursor.fake)
        return;

    parent_x = x - xwl_seat->cursor.fake->parent->window->drawable.x;
    parent_y = y - xwl_seat->cursor.fake->parent->window->drawable.y;
    origin_x = x - xwl_seat->cursor.fake->origin->window->drawable.x;
    origin_y = y - xwl_seat->cursor.fake->origin->window->drawable.y;

    zwp_locked_pointer_v1_set_cursor_position_hint(xwl_seat->cursor.fake->locked_pointer,
                                                   wl_fixed_from_int(origin_x),
                                                   wl_fixed_from_int(origin_y));

    wl_subsurface_set_position(xwl_seat->cursor.fake->subsurface, parent_x, parent_y);
    wl_surface_commit(xwl_seat->cursor.fake->surface);
}
