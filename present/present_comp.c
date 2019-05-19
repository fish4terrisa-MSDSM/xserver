/*
 * Copyright © 2019 Roman Gilg
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

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "present_priv.h"
#include "compint.h"

/*
 * Auto-list compositing.
 *
 * With Present Extension 1.3 a compositing window manager might send a
 * non-empty "auto-list" to the XServer. That is a list of windows being
 * composited by the XServer automatically.
 *
 * Such windows can be processed by the Present extension in a more direct
 * way.
 *
 */

/* TODO:
 * - How clip to siblings when manual compositing
 * - How to handle damage of siblings when child-window presenting?
 * - Compositor content somehow still leaks through, maybe KWin issue though
 *   (paints over scanned out Pixmap before receiving idle event).
 */

/*
 * Returns for 'window' a valid present_window_priv_ptr if either 'window'
 * directly is marked at the moment as being auto-composited or an ancestor
 * window is. Otherwise NULL is returned.
 */
present_window_priv_ptr
present_comp_client_window(WindowPtr window)
{
    WindowPtr               ancestor;
    present_window_priv_ptr ancestor_priv;

    for (ancestor = window; ancestor; ancestor = ancestor->parent) {
        ancestor_priv = present_window_priv(ancestor);
        if (ancestor_priv && ancestor_priv->auto_target)
            return ancestor_priv;
    }
    return NULL;
}

void
present_comp_destroy_auto_client_vblank(present_vblank_ptr target, WindowPtr window, int notify_mode)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(target->screen);
    present_vblank_ptr      vblank;

    xorg_list_for_each_entry(vblank, &target->auto_clients_vblanks, auto_client_link) {
        if (vblank->window != window)
            continue;

        if (notify_mode >= 0) {
            uint64_t ust = 0, crtc_msc = 0;
            if (vblank->crtc)
                (*screen_priv->info->get_ust_msc)(vblank->crtc, &ust, &crtc_msc);
            present_vblank_notify(vblank, vblank->kind, notify_mode, ust, crtc_msc);
        }
        present_vblank_destroy(vblank);
        break;
    }
}

/* Returns if the window currently or about to auto-compositing other windows.
 */
static inline Bool
present_comp_is_target(present_window_priv_ptr window_priv)
{
    return !xorg_list_is_empty(&window_priv->auto_head);
}

static void
present_comp_destroy_target_buf(present_auto_buf_ptr buf)
{
    ScreenPtr               screen;
    present_screen_priv_ptr screen_priv;

    if (!buf->pixmap)
        return;

    screen = buf->pixmap->drawable.pScreen;
    screen_priv = present_screen_priv(screen);

    (*screen_priv->info->destroy_pixmap)(buf->driver_data);

    dixDestroyPixmap(buf->pixmap, buf->pixmap->drawable.id);
    present_fence_destroy(buf->idle_fence);
    memset(buf, 0, sizeof(present_auto_buf_rec));
}

/*
 * On client composite prepare the target by setup of its back buffer.
 */
static void
present_comp_setup_target_buf(WindowPtr window, int buffer_index)
{
    present_window_priv_ptr window_priv = present_window_priv(window);
    ScreenPtr               screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_auto_buf_ptr    buf = &window_priv->auto_target_buf[buffer_index];

    if (buf->pixmap) {
        if (buf->pixmap->drawable.width == window->drawable.width &&
                buf->pixmap->drawable.height == window->drawable.height) {
            /* Already prepared - we can just reuse the current buffer. */
            return;
        }
        /* Target window size changed, delete old buffer. */
        present_comp_destroy_target_buf(buf);
    }

    /* Allocate new back buffer. */
    buf->pixmap = (*screen_priv->info->create_pixmap)
            (present_get_crtc(window),
             &buf->driver_data,
             window->drawable.width,
             window->drawable.height);

    // TODO: check that buf->pixmap exists now

    /* Copy current target window content into it. */
    present_copy_region(&window->drawable,
                        &buf->pixmap->drawable,
                        NULL,
                        0, 0);
    buf->is_clean = TRUE;
}

/*
 * Replace the client buffer with a more recent one.
 */
static void
present_comp_update_client_buf(present_vblank_ptr vblank, present_auto_buf_ptr buf)
{
    /* first cleanup a possible back buffer */
    if (buf->pixmap) {
        if (buf->idle_fence)
            present_fence_set_triggered(buf->idle_fence);
        present_send_idle_notify(buf->window, buf->serial, buf->pixmap, buf->idle_fence);

        dixDestroyPixmap(buf->pixmap, buf->pixmap->drawable.id);
        present_fence_destroy(buf->idle_fence);
        memset(buf, 0, sizeof(present_auto_buf_rec));
    }

    if (!vblank)
        return;

    buf->pixmap = vblank->pixmap;
    buf->window = vblank->window;
    buf->serial = vblank->serial;
    buf->idle_fence = vblank->idle_fence;
    vblank->idle_fence = NULL;

    vblank->pixmap->refcnt++;
}

static void
present_comp_execute_target_buf(WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_auto_buf_rec    next_buf;

    present_comp_setup_target_buf(window, 1);
    next_buf = window_priv->auto_target_buf[1];

    if (!next_buf.is_clean) {
        /* Target update needs to be copied in from current buf. */
        present_copy_region(&window->drawable,
                            &next_buf.pixmap->drawable,
                            NULL,
                            0, 0);
        next_buf.is_clean = TRUE;
    }

    window_priv->auto_target_buf[1] = window_priv->auto_target_buf[0];
    window_priv->auto_target_buf[0] = next_buf;
}

/*
 * Update the target buffer with new data by the target.
 */
static void
present_comp_update_target_buf(present_vblank_ptr vblank)
{
    present_window_priv_ptr window_priv = present_window_priv(vblank->window);
    present_auto_buf_rec    render_buf;

    present_comp_setup_target_buf(vblank->window, 1);
    render_buf = window_priv->auto_target_buf[1];

    present_copy_region(&vblank->pixmap->drawable,
                        &render_buf.pixmap->drawable,
                        NULL,
                        0, 0);

    render_buf.is_clean = TRUE;
    window_priv->auto_target_buf[0].is_clean = FALSE;

    window_priv->auto_target_buf[1] = window_priv->auto_target_buf[0];
    window_priv->auto_target_buf[0] = render_buf;
}

/* Checks if the window has a vblank queued at the moment for 'msc' and
 * returns it.
 */
static present_vblank_ptr
present_comp_queued(present_window_priv_ptr target_priv, uint64_t msc)
{
    present_vblank_ptr vblank;

    xorg_list_for_each_entry(vblank, &target_priv->vblank, window_list) {
        if (!vblank->pixmap)
            continue;
        if (!vblank->queued)
            continue;
        if (vblank->target_msc != msc)
            continue;

        return vblank;
    }
    return NULL;
}

static void
present_comp_check_ancestor(present_window_priv_ptr window_priv)
{
    present_window_priv_ptr ancestor_priv;

    if (window_priv->auto_target)
        /* Auto-comp directly. */
        return;

    ancestor_priv = present_comp_client_window(window_priv->window);

    if (window_priv->auto_ancestor == ancestor_priv->window)
        /* No change. */
        return;

    /* Auto-ancestor has changed, do reset. */
    xorg_list_del(&window_priv->auto_descendant_node);  // TODO: necessary? there should have been an unmap!
    window_priv->auto_ancestor = NULL;

    /* There is a different ancestor now. */
    xorg_list_add(&window_priv->auto_descendant_node, &ancestor_priv->auto_descendant_head);
    window_priv->auto_ancestor = ancestor_priv->window;
}

Bool
present_comp_pixmap(present_vblank_ptr vblank,
                    uint64_t crtc_ust,
                    uint64_t crtc_msc)
{
    ScreenPtr               screen = vblank->screen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_window_priv_ptr window_priv = present_window_priv(vblank->window);
    present_window_priv_ptr target_priv = present_window_priv(vblank->auto_target);

    present_comp_check_ancestor(window_priv);

    assert(vblank->target_msc >= crtc_msc);

    if (vblank->target_msc == crtc_msc) {
        /* An async flip is requested for the current msc,
         * but a synced flip on the target would block us.
         */

        vblank->target_msc++;

        if (NULL == present_comp_queued(target_priv, vblank->target_msc)) {
            present_comp_setup_target_buf(target_priv->window, 1);
            screen_priv->present_comp(vblank,
                                      FALSE,
                                      NULL,
                                      NULL,
                                      0, 0,
                                      crtc_ust,
                                      crtc_msc);
        }

        present_comp_update_client_buf(vblank, &window_priv->auto_client_buf);

        /* Will be composited from window in next frame */
        present_vblank_notify(vblank,
                              PresentCompleteKindPixmap,
                              PresentCompleteModeFlip,
                              crtc_ust,     // TODO: this should be the expected screen time, but then
                              crtc_msc);    //       client waits till next frame sending update.
        present_vblank_destroy(vblank);

    } else {
        present_vblank_ptr target_vblank = present_comp_queued(target_priv,
                                                               vblank->target_msc);

        if (target_vblank) {
            /* Target vblank has been queued for the target msc. Attach to it. */
            present_comp_destroy_auto_client_vblank(target_vblank, window_priv->window,
                                                    PresentCompleteModeSkip);
            xorg_list_add(&vblank->auto_client_link, &target_vblank->auto_clients_vblanks);
        } else {
            /* Create a new internal Present run of the target window. */
            present_comp_setup_target_buf(target_priv->window, 1);
            screen_priv->present_comp(vblank,
                                      TRUE,
                                      NULL,
                                      NULL,
                                      0, 0,
                                      crtc_ust,
                                      crtc_msc);
        }
    }
    return TRUE;
}

/* Auto composite client window into target vblank.
 */
static void
present_comp_execute_client(present_vblank_ptr vblank, WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_window_priv_ptr child_priv;
    present_vblank_ptr      vbl;
    RegionPtr               clip_list = RegionDuplicate(&window->clipList);
    DrawablePtr             vbl_drawable = NULL;

    xorg_list_for_each_entry(vbl, &vblank->auto_clients_vblanks, auto_client_link) {
        RegionPtr damage;

        if (vbl->window != window)
            continue;

        vbl_drawable = &vbl->pixmap->drawable;

        if (vbl->update) {
            damage = vbl->update;
            vbl->update = NULL;
            RegionTranslate(damage,
                            window->drawable.x,
                            window->drawable.y);
            RegionIntersect(damage, damage, &window->clipList);
        } else {
            damage = RegionDuplicate(&window->clipList);
        }
        /* Report update region as damaged
         */
        // TODO: Is there need for a new damage event, such that the target gets not informed?
        DamageDamageRegion(&window->drawable, damage);

        present_copy_region(&vbl->pixmap->drawable,
                            &window->drawable,
                            NULL,
                            0, 0);

        RegionDestroy(damage);

        break;
    }

    if (vblank->update)
        // TODO: factor in target position
        RegionUnion(vblank->update, vblank->update, clip_list);

    RegionTranslate(clip_list, -window->drawable.x, -window->drawable.y);

    if (vbl_drawable) {
        present_copy_region(vbl_drawable,
                            &vblank->pixmap->drawable,
                            clip_list,
                            window->drawable.x, window->drawable.y);
    } else {
        if (window_priv->auto_client_buf.pixmap) {
            present_auto_buf_rec buf = window_priv->auto_client_buf;

            memset(&window_priv->auto_client_buf, 0, sizeof(present_auto_buf_rec));

            present_copy_region(&buf.pixmap->drawable,
                                &window->drawable,
                                NULL,
                                0, 0);

            /* Report update region as damaged
             */
            // TODO: Is there need for a new damage event, such that the target gets not informed?
            DamageDamageRegion(&window->drawable, clip_list);

            present_copy_region(&buf.pixmap->drawable,
                                &vblank->pixmap->drawable,
                                clip_list,
                                window->drawable.x, window->drawable.y);

            present_comp_update_client_buf(NULL, &buf);

        } else {
            present_copy_region(&window->drawable,
                                &vblank->pixmap->drawable,
                                clip_list,
                                window->drawable.x, window->drawable.y);
        }
    }

    xorg_list_for_each_entry(child_priv, &window_priv->auto_descendant_head, auto_descendant_node) {
        // TODO: how can we clip these to parent and siblings in auto-composite mode?
        present_comp_execute_client(vblank, child_priv->window);
    }
}

/* On Present execute auto composite client windows into it.
 */
void
present_comp_execute_target(present_vblank_ptr vblank)
{
    WindowPtr window = vblank->window;
    present_window_priv_ptr window_priv = present_window_priv(window), elm;

    if (!present_comp_is_target(window_priv))
        return;

    if (vblank->auto_internal) {
        present_comp_execute_target_buf(window);
    } else {
        present_comp_update_target_buf(vblank);
    }

    // TODO: are these lists in stacking order? Do we stack at all?
    xorg_list_for_each_entry(elm, &window_priv->auto_head, auto_node) {
        present_comp_execute_client(vblank, elm->window);
    }
}

static void
present_comp_to_auto(WindowPtr window)
{
    ScreenPtr               screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_vblank_ptr      vblank, tmp;
    uint64_t                ust = 0;
    uint64_t                crtc_msc = 0;

    /* Try to auto-composite queued vblanks. */
    xorg_list_for_each_entry_safe(vblank, tmp, &window_priv->vblank, window_list) {
        if (vblank->flip_ready)
            /* Ignore ready flips to avoid pitfalls. */
            continue;

        if (Success != (*screen_priv->info->get_ust_msc)(vblank->crtc, &ust, &crtc_msc))
            continue;

        if (vblank->target_msc <= crtc_msc)
            continue;

        xorg_list_del(&vblank->event_queue);
        vblank->auto_target = window_priv->auto_target;
        present_comp_pixmap(vblank, ust, crtc_msc);
    }
}

/* Switch window from being composited automatically
 * by the XServer to being composited by the window
 * manager.
 */
static void
present_comp_to_manual(WindowPtr window)
{
    ScreenPtr               screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_window_priv_ptr child, tmp_child;
    present_vblank_ptr      vblank, tmp_vblank;

    xorg_list_for_each_entry_safe(child, tmp_child, &window_priv->auto_descendant_head, auto_descendant_node) {
        /* Switch child Windows of 'window' as an auto-compositing ancestor. */
        present_comp_to_manual(child->window);
    }

    /* Deactive auto-composite for queued vblanks. */
    xorg_list_for_each_entry_safe(vblank, tmp_vblank, &window_priv->vblank, window_list) {
        xorg_list_del(&vblank->auto_client_link);
        vblank->auto_target = NULL;
        screen_priv->re_execute(vblank);
    }

    xorg_list_del(&window_priv->auto_descendant_node);
    window_priv->auto_ancestor = NULL;
    window_priv->auto_target = NULL;
}

// TODO: destroy client buffer

static void
present_comp_check_destroy_target_buf(WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);

    if (present_comp_is_target(window_priv))
        return;
    present_comp_destroy_target_buf(&window_priv->auto_target_buf[0]);
    present_comp_destroy_target_buf(&window_priv->auto_target_buf[1]);
}

/* Handle request from compositor to update the auto list (this only affects
 * the render auto list for the next frame).
 */
int
present_comp_set_auto_list(ClientPtr client, WindowPtr target, int nwindows, Window *windows)
{
    ScreenPtr               screen = target->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    WindowPtr               window;
    present_window_priv_ptr target_priv, window_priv, elm;
    int                     i, rc;
    struct xorg_list        old_auto;

    if (!screen_priv->info || !screen_priv->info->create_pixmap)
        return BadRequest;

    xorg_list_init(&old_auto);

    target_priv = present_get_window_priv(target, TRUE);
    if (!target_priv)
        return BadAlloc;

    /* Reset the auto list. We put all windows temporarily in old_auto. */
    if (!xorg_list_is_empty(&target_priv->auto_head)) {
        old_auto.next = target_priv->auto_head.next;
        old_auto.prev = target_priv->auto_head.prev;
        old_auto.next->prev = &old_auto;
        old_auto.prev->next = &old_auto;
        xorg_list_init(&target_priv->auto_head);
    }

    for (i = 0; i < nwindows; i++) {
        rc = dixLookupWindow(&window, windows[i], client, DixReadAccess);
        if (rc != Success)
            goto err_out;

        // TODO: check that we don't have cyclic relations between auto composited windows
        // maybe check if 'parent' is really the parent window for all windows (or an overlay)?

        window_priv = present_get_window_priv(window, TRUE);
        if (!window_priv) {
            rc = BadAlloc;
            goto err_out;
        }

        if (window_priv->auto_target && window_priv->auto_target != target_priv->window) {
            /* window already being auto-composited into another target */
            rc = BadAccess;
            goto err_out;
        }

        window_priv->auto_target = target;

        if (xorg_list_is_empty(&window_priv->auto_node)) {
            /* This  window was not auto-composited before. */
            present_comp_to_auto(window);
        } else {
            /* This removes it from old_auto list. */
            xorg_list_del(&window_priv->auto_node);
        }
        xorg_list_append(&window_priv->auto_node, &target_priv->auto_head);
    }

    xorg_list_for_each_entry(elm, &old_auto, auto_node) {
        /* Cleanup windows, which were removed from the auto-list. */
        present_comp_to_manual(elm->window);
    }

    if (!present_comp_is_target(target_priv)) {
        present_comp_destroy_target_buf(&target_priv->auto_target_buf[0]);
        present_comp_destroy_target_buf(&target_priv->auto_target_buf[1]);
    }

    return Success;

err_out:
    xorg_list_for_each_entry(elm, &old_auto, auto_node) {
        present_comp_to_manual(elm->window);
    }
    xorg_list_for_each_entry(elm, &target_priv->auto_head, auto_node) {
        present_comp_to_manual(elm->window);
    }
    xorg_list_del(&target_priv->auto_head);
    present_comp_check_destroy_target_buf(target);
    return rc;
}

/*
 * Cleanup of 'window_priv' as a client window directly or through an ancestor.
 */
static void
present_comp_cleanup_above(WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);

    if (window_priv->auto_ancestor) {
        present_window_priv_ptr ancestor_priv = present_window_priv(window_priv->auto_ancestor);
        /* A parent Window was/is being auto-composited, but not this Window directly. */
        assert(ancestor_priv);
        assert(ancestor_priv->auto_target);
        assert(!window_priv->auto_target);

        xorg_list_del(&window_priv->auto_descendant_node);
        present_comp_check_destroy_target_buf(ancestor_priv->auto_target);
        window_priv->auto_ancestor = NULL;

    } else if (window_priv->auto_target) {
        /* This Window was auto-composited directly. */
        xorg_list_del(&window_priv->auto_node);

        present_comp_check_destroy_target_buf(window_priv->auto_target);
        window_priv->auto_target = NULL;
    }
}

/*
 * Cleanup of 'window_priv' as a target or ancestor window.
 */
static void
present_comp_cleanup_below(present_window_priv_ptr window_priv)
{
    present_window_priv_ptr element, tmp;

    /* Cleanup this window as an auto-composited target for other windows. */
    xorg_list_for_each_entry(element, &window_priv->auto_head, auto_node) {
        present_comp_to_manual(element->window);
    }
    xorg_list_del(&window_priv->auto_head);

    /* Cleanup this window as an auto-ancestor for other windows. */
    xorg_list_for_each_entry_safe(element, tmp, &window_priv->auto_descendant_head, auto_descendant_node) {
        present_comp_to_manual(element->window);
    }
    xorg_list_del(&window_priv->auto_descendant_head);

    /* Now we can cleanup this window as a target. */
    present_comp_check_destroy_target_buf(window_priv->window);
}

void
present_comp_cleanup_window(WindowPtr window)
{
    present_window_priv_ptr window_priv;

    window_priv = present_window_priv(window);
    if (!window_priv)
        return;

    /* First cleanup connections with other Windows depending on this one. */
    present_comp_cleanup_below(window_priv);
    /* Then cleanup connections to a possible target this Window depends on. */
    present_comp_cleanup_above(window);
}
