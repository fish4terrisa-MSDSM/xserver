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

#ifndef XWAYLAND_CURSOR_FAKE_H
#define XWAYLAND_CURSOR_FAKE_H

#include <xwayland-config.h>
#include <xwayland-input.h>

#include <dix.h>

#include "xwayland-types.h"

void xwl_cursor_fake_update_focus(struct xwl_screen *xwl_screen, WindowPtr window);
void xwl_cursor_destroy_fake(struct xwl_seat *xwl_seat);
void xwl_cursor_set_fake_cursor(struct xwl_seat *xwl_seat);
void xwl_cursor_fake_position(struct xwl_seat *xwl_seat, int x, int y);

#endif /* XWAYLAND_CURSOR_FAKE_H */
