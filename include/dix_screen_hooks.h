/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2024 Enrico Weigelt, metux IT consult <info@metux.net>
 *
 * @brief exported API entry points for hooking into screen operations
 *
 * These hooks are replacing the old, complicated approach of wrapping
 * ScreenRec's proc vectors. Unlike the wrapping, these hooks are designed
 * to be safe against changes in setup/teardown order and are called
 * independently of the ScreenProc call vectors. It is guaranteed that the
 * objects to operate on already/still exist (eg. destructors are callled
 * before the object is actually destroyed, while post-create hooks are
 * called after the object is created)
 *
 * Main consumers are extensions that need to associate extra data or
 * doing other things additional to the original operation. In some cases
 * they might even be used in drivers (in order to split device specific
 * from generic logic)
 */
#ifndef DIX_SCREEN_HOOKS_H
#define DIX_SCREEN_HOOKS_H

#include <X11/Xfuncproto.h>

#include "screenint.h" /* ScreenPtr */
#include "window.h" /* WindowPtr */

/* prototype of a window destructor */
typedef void (*XorgWindowDestroyProcPtr)(ScreenPtr pScreen,
                                         WindowPtr pWindow,
                                         void *arg);

/**
 * @brief register a window on the given screen
 *
 * @param pScreen pointer to the screen to register the destructor into
 * @param func pointer to the window destructor function
 * @param arg opaque pointer passed to the destructor
 *
 * Window destructors are the replacement for fragile and complicated wrapping of
 * pScreen->DestroyWindow(): extensions can safely register there custom destructors
 * here, without ever caring about anybody else.
 +
 * The destructors are run right before pScreen->DestroyWindow() - when the window
 * is already removed from hierarchy (thus cannot receive any events anymore) and
 * most of it's data already destroyed - and supposed to do necessary per-extension
 * cleanup duties. Their execution order is *unspecified*.
 *
 * Screen drivers (DDX'es, xf86 video drivers, ...) shall not use these, but still
 * set the pScreen->DestroyWindow pointer - and these should be the *only* ones
 * ever setting it.
 *
 * When registration fails, the server aborts.
 *
 **/
_X_EXPORT void dixScreenHookWindowDestroy(ScreenPtr pScreen,
                                          XorgWindowDestroyProcPtr func,
                                          void *arg);

/**
 * @brief unregister a window destructor on the given screen
 *
 * @param pScreen pointer to the screen to unregister the destructor from
 * @param func pointer to the window destructor function
 * @param arg opaque pointer passed to the destructor
 *
 * @see dixScreenHookWindowDestroy
 *
 * Unregister a window destructor hook registered via @ref dixScreenHookWindowDestroy
 **/
_X_EXPORT void dixScreenUnhookWindowDestroy(ScreenPtr pScreen,
                                            XorgWindowDestroyProcPtr func,
                                            void *arg);

/* prototype of a window move notification handler */
typedef void (*XorgWindowPositionProcPtr)(ScreenPtr pScreen,
                                          WindowPtr pWindow,
                                          void *arg,
                                          int32_t x,
                                          int32_t y);

/**
 * @brief register a position notify hook on the given screen
 *
 * @param pScreen pointer to the screen to register the notify hook into
 * @param func pointer to the window hook function
 * @param arg opaque pointer passed to the hook
 *
 * When registration fails, the server aborts.
 *
 **/
_X_EXPORT void dixScreenHookWindowPosition(ScreenPtr pScreen,
                                           XorgWindowPositionProcPtr func,
                                           void *arg);

/**
 * @brief unregister a window position notify hook on the given screen
 *
 * @param pScreen pointer to the screen to unregister the hook from
 * @param func pointer to the hook function
 * @param arg opaque pointer passed to the destructor
 *
 * @see dixScreenHookWindowPosition
 *
 * Unregister a window position notify hook registered via @ref dixScreenHookWindowPosition
 **/
_X_EXPORT void dixScreenUnhookWindowPosition(ScreenPtr pScreen,
                                             XorgWindowPositionProcPtr func,
                                             void *arg);

#endif /* DIX_SCREEN_HOOKS_H */
