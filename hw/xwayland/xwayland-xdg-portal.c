/*
 * Copyright © 2020 Red Hat, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *      Olivier Fourdan <ofourdan@redhat.com>
 */

#include <xwayland-config.h>

#include <dbus/dbus.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "os.h"
#include "dbus-core.h"
#include "xwayland-xdg-portal.h"

#define DESKTOP_PATH   "/org/freedesktop/portal/desktop"
#define REQUEST_PATH   DESKTOP_PATH "/request/%s/%s"
#define SESSION_PATH   DESKTOP_PATH "/session/%s/%s"
#define PORTAL_DESKTOP "org.freedesktop.portal.Desktop"
#define PORTAL_SESSION "org.freedesktop.portal.Session"
#define PORTAL_REQUEST "org.freedesktop.portal.Request"
#define REMOTE_DESKTOP "org.freedesktop.portal.RemoteDesktop"

#define REMOTE_DESKTOP_KEYBOARD    (1 << 0)
#define REMOTE_DESKTOP_POINTER     (1 << 1)

#define EMPTY_VARDICT { NULL, DBUS_TYPE_INVALID, NULL }
#define EMPTY_STRING  ""

enum remote_desktop_state {
    REMOTE_DESKTOP_STATE_NEW = 0,
    REMOTE_DESKTOP_STATE_INIT,
    REMOTE_DESKTOP_STATE_SESSION_HANDLE,
    REMOTE_DESKTOP_STATE_SELECT_DEVICE,
    REMOTE_DESKTOP_STATE_START,
    REMOTE_DESKTOP_STATE_STARTED,
    REMOTE_DESKTOP_STATE_FAILED,
};

struct remote_desktop_info {
    DBusConnection           *connection;
    char                     *session_path;
    enum remote_desktop_state state;
};

static struct remote_desktop_info remote_desktop_info;

struct vardict {
    const char *key;
    char        type;
    void       *value;
};

static char *
get_sender_name(void)
{
    char *sender_name;
    char *s;

    /* SENDER is the callers unique name, with the initial ':' removed
     * and all '.' replaced by '_'
     */
    sender_name = strdup(dbus_bus_get_unique_name(remote_desktop_info.connection) + 1);
    if (!sender_name)
        return NULL;

    s = sender_name;
    while ((s = strchr(s, '.')))
      *s = '_';

    return (sender_name);
}

static char *
get_unique_token(void)
{
    char *token;
    unsigned int token_rand;

    token_rand = (unsigned int) (65536.0 / (RAND_MAX + 1.0) * rand());

    token = calloc(16, 1);
    if (!token)
        return NULL;

    snprintf(token, 16, "Xwayland_0x%04X", token_rand);

    return (token);
}

static char *
get_unique_path(const char *token)
{
    char *sender_name;
    char *request_path;

    request_path = calloc(255, 1);
    if (!request_path)
        return NULL;

    sender_name = get_sender_name();
    if (!sender_name) {
        free(request_path);
        return NULL;
    }

    snprintf(request_path, 255, REQUEST_PATH, sender_name, token);

    free(sender_name);

    return request_path;
}

static dbus_bool_t
portal_remotedesktop_add_response_match(const char *path)
{
    DBusError dbus_error;
    char *rule;

    rule = calloc(255, 1);
    if (!rule)
        return FALSE;

    snprintf(rule, 255,
        "type='signal',"
        "sender='" PORTAL_DESKTOP "',"
        "path='%s',"
        "interface='" PORTAL_REQUEST "',"
        "member='Response'", path);

    dbus_error_init(&dbus_error);

    dbus_bus_add_match(remote_desktop_info.connection, rule, &dbus_error);

    if (dbus_error_is_set(&dbus_error))
        ErrorF("XDG portal: Failed to add match: '%s' (%s)\n",
               dbus_error.name, dbus_error.message);

    free(rule);

    return !dbus_error_is_set(&dbus_error);
}

static dbus_int32_t
get_response_status(DBusMessageIter *iter)
{
    int arg_type;
    dbus_int32_t val;

    arg_type = dbus_message_iter_get_arg_type(iter);
    if (arg_type != DBUS_TYPE_UINT32)
        return 2;

    dbus_message_iter_get_basic(iter, &val);
    LogMessage(X_DEBUG, "Response status = %u\n", val);

    return val;
}

static const char*
get_basic_type_as_string(char type)
{
    switch (type) {
        case DBUS_TYPE_BYTE:
            return DBUS_TYPE_BYTE_AS_STRING;
            break;
        case DBUS_TYPE_BOOLEAN:
            return DBUS_TYPE_BOOLEAN_AS_STRING;
            break;
        case DBUS_TYPE_INT16:
            return DBUS_TYPE_INT16_AS_STRING;
            break;
        case DBUS_TYPE_UINT16:
            return DBUS_TYPE_UINT16_AS_STRING;
            break;
        case DBUS_TYPE_INT32:
            return DBUS_TYPE_INT32_AS_STRING;
            break;
        case DBUS_TYPE_UINT32:
            return DBUS_TYPE_UINT32_AS_STRING;
            break;
        case DBUS_TYPE_INT64:
            return DBUS_TYPE_INT64_AS_STRING;
            break;
        case DBUS_TYPE_UINT64:
            return DBUS_TYPE_UINT64_AS_STRING;
            break;
        case DBUS_TYPE_DOUBLE:
            return DBUS_TYPE_DOUBLE_AS_STRING;
            break;
        case DBUS_TYPE_STRING:
            return DBUS_TYPE_STRING_AS_STRING;
            break;
        case DBUS_TYPE_OBJECT_PATH:
            return DBUS_TYPE_OBJECT_PATH_AS_STRING;
            break;
        default:
            ErrorF("XDG portal: Unsupported type\n");
    }
    return NULL;
}

static dbus_bool_t
get_response_vardict(DBusMessage    *msg,
                     struct vardict *vardict)
{
    DBusMessageIter iter;
    DBusMessageIter iter_array;
    DBusMessageIter iter_dict_entry;
    DBusMessageIter iter_val;
    int arg_type;
    char *value = NULL;

    if (!dbus_message_iter_init(msg, &iter))
        return FALSE;

    if (get_response_status(&iter) != 0)
        return FALSE;

    if (!dbus_message_iter_next(&iter))
        return FALSE;

    arg_type = dbus_message_iter_get_arg_type(&iter);
    if (arg_type != DBUS_TYPE_ARRAY)
        return FALSE;

    dbus_message_iter_recurse(&iter, &iter_array);

    arg_type = dbus_message_iter_get_arg_type(&iter_array);
    while (arg_type == DBUS_TYPE_DICT_ENTRY) {
        const char *result_name;
        dbus_message_iter_recurse(&iter_array, &iter_dict_entry);

        if (dbus_message_iter_get_arg_type(&iter_dict_entry) != DBUS_TYPE_STRING)
            return FALSE;

        dbus_message_iter_get_basic(&iter_dict_entry, &result_name);

        if (vardict->key && !strcmp (result_name, vardict->key)) {
            char *variant_signature;

            dbus_message_iter_next(&iter_dict_entry);
            dbus_message_iter_recurse(&iter_dict_entry, &iter_val);

            variant_signature = dbus_message_iter_get_signature(&iter_val);
            if (!variant_signature)
                return FALSE;

            if (strcmp(variant_signature, get_basic_type_as_string(vardict->type))) {
                ErrorF("XDG portal: Wrong signature, expecting '%s', got '%s'\n",
                        get_basic_type_as_string(vardict->type), variant_signature);
                dbus_free(variant_signature);
                return FALSE;
            }

            if (!strcmp(variant_signature, DBUS_TYPE_STRING_AS_STRING) ||
                !strcmp(variant_signature, DBUS_TYPE_OBJECT_PATH_AS_STRING)) {
                const char *variant_value;

                dbus_message_iter_get_basic(&iter_val, &variant_value);
                if (variant_value) {
                    *((char **) vardict->value) = (char *) calloc(strlen(variant_value) + 1, 1);
                    if (*((char **) vardict->value))
                        strcpy(*((char **) vardict->value), variant_value);
                }
            } else if (dbus_type_is_basic(vardict->type)) {
                dbus_message_iter_get_basic(&iter_val, vardict->value);
            } else {
                ErrorF("XDG portal: Unsupported signature '%s'\n", variant_signature);
                dbus_free(variant_signature);
                return FALSE;
            }
            dbus_free(variant_signature);
        }

        if (value)
            break;

        dbus_message_iter_next(&iter_array);

        arg_type = dbus_message_iter_get_arg_type(&iter_array);
    }

    return TRUE;
}

static dbus_bool_t
add_vardict(DBusMessageIter *iter,
            struct vardict   dict_array[])
{
    DBusMessageIter iter_array;
    DBusMessageIter iter_dict_entry;
    DBusMessageIter iter_val;
    int i;

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
                                          DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                          DBUS_TYPE_STRING_AS_STRING
                                          DBUS_TYPE_VARIANT_AS_STRING
                                          DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                          &iter_array))
        return FALSE;

    if (!dbus_message_iter_open_container(&iter_array,
	                                  DBUS_TYPE_DICT_ENTRY, NULL,
	                                  &iter_dict_entry))
        return FALSE;

    i = 0;
    while (dict_array[i].key) {
        if (!dbus_message_iter_open_container(&iter_array,
                                              DBUS_TYPE_DICT_ENTRY, NULL,
                                              &iter_dict_entry))
           return FALSE;

        if (!dbus_message_iter_append_basic(&iter_dict_entry, DBUS_TYPE_STRING, &(dict_array[i].key)))
            return FALSE;

        if (!dbus_message_iter_open_container(&iter_dict_entry, DBUS_TYPE_VARIANT,
                                              get_basic_type_as_string(dict_array[i].type), &iter_val))
            return FALSE;

        if (!dbus_message_iter_append_basic(&iter_val, dict_array[i].type, dict_array[i].value))
            return FALSE;

        if (!dbus_message_iter_close_container(&iter_dict_entry, &iter_val))
            return FALSE;

        if (!dbus_message_iter_close_container(&iter_array, &iter_dict_entry))
            return FALSE;

        i++;
    }

    if (!dbus_message_iter_close_container(iter, &iter_array))
        return FALSE;

    return TRUE;
}

static void
on_create_session_reply(DBusPendingCall *pending, void *user_data)
{
    DBusMessage *reply = NULL;
    DBusError dbus_error;
    char *object_path = NULL;
    char *request_path = (char *) user_data;

    reply = dbus_pending_call_steal_reply(pending);
    if (!reply) {
        ErrorF("XDG portal: no reply\n");
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(reply, &dbus_error, DBUS_TYPE_OBJECT_PATH, &object_path,
                               DBUS_TYPE_INVALID)) {
        ErrorF("XDG portal: Error getting session reply args: '%s' (%s)\n",
               dbus_error.name, dbus_error.message);
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    if (!object_path) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    if (strcmp (request_path, object_path)) {
        if (!portal_remotedesktop_add_response_match(object_path))
            remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
    }

    LogMessage(X_DEBUG, "object_path = '%s'\n", object_path);
    remote_desktop_info.state = REMOTE_DESKTOP_STATE_SESSION_HANDLE;

out:
    free(request_path);
    if (reply)
        dbus_message_unref(reply);
}

static void
portal_remotedesktop_create_session(const char *token)
{
    DBusMessage *msg = NULL;
    DBusPendingCall *pending;
    DBusMessageIter iter;
    char *request_path = NULL;
    struct vardict dict_array[] = {
        { "session_handle_token", DBUS_TYPE_STRING, &token },
        { "handle_token", DBUS_TYPE_STRING, &token },
        EMPTY_VARDICT,
    };

    request_path = get_unique_path(token);
    if (!request_path) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }
    LogMessage(X_DEBUG, "request path %s\n", request_path);

    if (!portal_remotedesktop_add_response_match(request_path)) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        free(request_path);
        goto out;
    }

    msg = dbus_message_new_method_call(PORTAL_DESKTOP,
                                       DESKTOP_PATH,
                                       REMOTE_DESKTOP,
                                       "CreateSession");
    if (!msg) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    dbus_message_iter_init_append(msg, &iter);

    if (!add_vardict(&iter, dict_array)) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        free(request_path);
        goto out;
    }

    if (!dbus_connection_send_with_reply(remote_desktop_info.connection,
                                         msg, &pending, DBUS_TIMEOUT_USE_DEFAULT)) {
        ErrorF("XDG portal: Error sending message: %s\n", strerror(errno));
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        free(request_path);
        goto out;
    }

    if (!pending) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        free(request_path);
        goto out;
    }

    dbus_pending_call_set_notify(pending, on_create_session_reply, request_path, free);

out:
    if (msg)
        dbus_message_unref(msg);
}

static void
portal_remotedesktop_select_devices(dbus_int32_t devices)
{
    DBusMessage *msg = NULL;
    DBusMessageIter iter;
    char *token = get_unique_token();
    struct vardict dict_array[] = {
        { "types", DBUS_TYPE_UINT32, &devices },
        { "handle_token", DBUS_TYPE_STRING, &token },
        EMPTY_VARDICT,
    };

    if (!token) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    msg = dbus_message_new_method_call(PORTAL_DESKTOP,
                                       DESKTOP_PATH,
                                       REMOTE_DESKTOP,
                                       "SelectDevices");
    if (!msg) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    dbus_message_iter_init_append(msg, &iter);

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                        &remote_desktop_info.session_path)) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    if (!add_vardict(&iter, dict_array)) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    if (!dbus_connection_send(remote_desktop_info.connection, msg, NULL)) {
        ErrorF("XDG portal: Error sending message: %s\n", strerror(errno));
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    remote_desktop_info.state = REMOTE_DESKTOP_STATE_SELECT_DEVICE;

out:
    free(token);
    if (msg)
        dbus_message_unref(msg);
}

static void
portal_remotedesktop_start(void)
{
    DBusMessage *msg = NULL;
    DBusMessageIter iter;
    char *arg;
    char *token = get_unique_token();
    struct vardict dict_array[] = {
        { "handle_token", DBUS_TYPE_STRING, &token },
        EMPTY_VARDICT,
    };

    if (!token) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    msg = dbus_message_new_method_call(PORTAL_DESKTOP,
                                       DESKTOP_PATH,
                                       REMOTE_DESKTOP,
                                       "Start");
    if (!msg) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    dbus_message_iter_init_append(msg, &iter);

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                        &remote_desktop_info.session_path)) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    arg = (char *) EMPTY_STRING;
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &arg)) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    if (!add_vardict(&iter, dict_array)) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    if (!dbus_connection_send(remote_desktop_info.connection, msg, NULL)) {
        ErrorF("XDG portal: Error sending message: %s\n", strerror(errno));
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    remote_desktop_info.state = REMOTE_DESKTOP_STATE_START;

out:
    free(token);
    if (msg)
        dbus_message_unref(msg);
}

static dbus_bool_t
portal_remotedesktop_maybe_init(void)
{
    char *token = NULL;

    if (remote_desktop_info.connection == NULL)
        return FALSE;

    if (remote_desktop_info.state == REMOTE_DESKTOP_STATE_STARTED)
        return TRUE;

    /* Whatever happens, we try only once */
    if (remote_desktop_info.state != REMOTE_DESKTOP_STATE_NEW)
        return FALSE;

    remote_desktop_info.state = REMOTE_DESKTOP_STATE_INIT;

    srand((unsigned int) getpid());

    token = get_unique_token();
    if (!token) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        goto out;
    }

    LogMessage(X_DEBUG, "token = '%s'\n", token);

    portal_remotedesktop_create_session(token);

out:
    free(token);

    return FALSE;
}

static void
portal_remotedesktop_reset(void)
{
    free(remote_desktop_info.session_path);
    remote_desktop_info.session_path = NULL;
    remote_desktop_info.state = REMOTE_DESKTOP_STATE_NEW;
}

static void
on_session_handle_reponse(DBusMessage *msg)
{
    struct vardict vardict;
    int status;

    remote_desktop_info.session_path = NULL;
    vardict.key = "session_handle";
    vardict.type= DBUS_TYPE_STRING;
    vardict.value = &remote_desktop_info.session_path;
    status = get_response_vardict(msg, &vardict);

    if (!remote_desktop_info.session_path || !status) {
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
        return;
    }

    LogMessage(X_DEBUG, "session_path = '%s'\n", remote_desktop_info.session_path);

    portal_remotedesktop_select_devices(REMOTE_DESKTOP_KEYBOARD |
                                        REMOTE_DESKTOP_POINTER);
}

static void
on_select_device_reponse(DBusMessage *msg)
{
    portal_remotedesktop_start();
}

static void
on_start_reponse(DBusMessage *msg)
{
    dbus_uint32_t devices;
    struct vardict vardict;
    int status;

    devices = 0;
    vardict.key = "devices";
    vardict.type = DBUS_TYPE_UINT32;
    vardict.value = &devices;
    status = get_response_vardict(msg, &vardict);
    LogMessage(X_DEBUG, "Devices selected: 0x%x\n", devices);

    if (status)
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_STARTED;
    else
        remote_desktop_info.state = REMOTE_DESKTOP_STATE_FAILED;
}

static DBusHandlerResult
message_filter(DBusConnection *connection, DBusMessage *msg, void *data)
{
    DBusError dbus_error;
    DBusHandlerResult ret = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
        char *name, *old_owner, *new_owner;

        dbus_error_init(&dbus_error);
        dbus_message_get_args(msg, &dbus_error,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_STRING, &old_owner,
                              DBUS_TYPE_STRING, &new_owner,
                              DBUS_TYPE_INVALID);

        if (dbus_error_is_set(&dbus_error)) {
            ErrorF("XDG portal: NameOwnerChanged failed: '%s' (%s)\n",
                   dbus_error.name, dbus_error.message);
        }
        else if (name && strcmp(name, PORTAL_DESKTOP) == 0) {
            if (!old_owner || !strlen(old_owner)) {
                LogMessage(X_DEBUG, "Portal started\n");
            }
            else {
                LogMessage(X_DEBUG, "Portal stopped or restarted\n");
                portal_remotedesktop_reset();
            }

            ret = DBUS_HANDLER_RESULT_HANDLED;
        }
        dbus_error_free(&dbus_error);
    }
    else if (dbus_message_is_signal(msg, PORTAL_REQUEST, "Response")) {

        LogMessage(X_DEBUG, "Portal request response received\n");

        switch (remote_desktop_info.state) {
            case REMOTE_DESKTOP_STATE_FAILED:
                /* Ignore */
                break;
            case REMOTE_DESKTOP_STATE_SESSION_HANDLE:
                on_session_handle_reponse(msg);
                break;
            case REMOTE_DESKTOP_STATE_SELECT_DEVICE:
                on_select_device_reponse(msg);
                break;
            case REMOTE_DESKTOP_STATE_START:
                on_start_reponse(msg);
                break;
            default:
                ErrorF("XDG portal: Unexpected response in state %i\n", remote_desktop_info.state);
        }
        ret = DBUS_HANDLER_RESULT_HANDLED;
    }
    else if (dbus_message_is_signal(msg, PORTAL_SESSION, "Closed")) {

        LogMessage(X_DEBUG, "Portal session closed received\n");
        portal_remotedesktop_reset();

        ret = DBUS_HANDLER_RESULT_HANDLED;
    }

    return ret;
}

static void
connect_hook(DBusConnection *connection, void *data)
{
    DBusError dbus_error;
    const char MATCH_RULE[] = "sender='org.freedesktop.DBus',"
        "interface='org.freedesktop.DBus',"
        "type='signal',"
        "path='/org/freedesktop/DBus'," "member='NameOwnerChanged'";

    remote_desktop_info.connection = connection;

    dbus_error_init(&dbus_error);
    dbus_bus_add_match(connection, MATCH_RULE, &dbus_error);
    if (dbus_error_is_set(&dbus_error))
        ErrorF("XDG portal: Failed to add match: '%s' (%s)\n",
               dbus_error.name, dbus_error.message);
    dbus_error_free(&dbus_error);

    if (!dbus_connection_add_filter(connection, message_filter, &remote_desktop_info, NULL))
        ErrorF("XDG portal: failed not add D-Bus filter\n");
}

static void
disconnect_hook(void *data)
{
    free(remote_desktop_info.session_path);
    remote_desktop_info.session_path = NULL;

    remote_desktop_info.connection = NULL;
    remote_desktop_info.state = REMOTE_DESKTOP_STATE_NEW;
}

static struct dbus_core_hook hook = {
    .connect = connect_hook,
    .disconnect = disconnect_hook,
    .data = &remote_desktop_info,
};

Bool
portal_remotedesktop_notify_pointer_motion(double dx,
                                           double dy)
{
    DBusMessage *msg = NULL;
    DBusMessageIter iter;
    dbus_bool_t status = FALSE;
    struct vardict dict_array[] = { EMPTY_VARDICT };

    if (!portal_remotedesktop_maybe_init())
        return FALSE;

    msg = dbus_message_new_method_call(PORTAL_DESKTOP,
                                       DESKTOP_PATH,
                                       REMOTE_DESKTOP,
                                       "NotifyPointerMotion");
    if (!msg)
        goto out;

    dbus_message_iter_init_append(msg, &iter);

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &remote_desktop_info.session_path))
        goto out;

    if (!add_vardict(&iter, dict_array))
        goto out;

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_DOUBLE, &dx))
        goto out;

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_DOUBLE, &dy))
        goto out;

    if (dbus_connection_send(remote_desktop_info.connection, msg, NULL))
        status = TRUE;

out:
    if (msg)
        dbus_message_unref(msg);

    return status;
}

Bool
portal_remotedesktop_notify_pointer_button(int          button,
                                           unsigned int state)
{
    DBusMessage *msg = NULL;
    DBusMessageIter iter;
    dbus_bool_t status = FALSE;
    struct vardict dict_array[] = { EMPTY_VARDICT };

    if (!portal_remotedesktop_maybe_init())
        return FALSE;

    msg = dbus_message_new_method_call(PORTAL_DESKTOP,
                                       DESKTOP_PATH,
                                       REMOTE_DESKTOP,
                                       "NotifyPointerButton");
    if (!msg)
        goto out;

    dbus_message_iter_init_append(msg, &iter);

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &remote_desktop_info.session_path))
        goto out;

    if (!add_vardict(&iter, dict_array))
        goto out;

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &button))
        goto out;

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &state))
        goto out;

    if (dbus_connection_send(remote_desktop_info.connection, msg, NULL))
        status = TRUE;

out:
    if (msg)
        dbus_message_unref(msg);

    return status;
}

Bool
portal_remotedesktop_notify_keyboard_keycode(int          keycode,
                                             unsigned int state)
{
    DBusMessage *msg = NULL;
    DBusMessageIter iter;
    dbus_bool_t status = FALSE;
    struct vardict dict_array[] = { EMPTY_VARDICT };

    if (!portal_remotedesktop_maybe_init())
        return FALSE;

    msg = dbus_message_new_method_call(PORTAL_DESKTOP,
                                       DESKTOP_PATH,
                                       REMOTE_DESKTOP,
                                       "NotifyKeyboardKeycode");
    if (!msg)
        goto out;

    dbus_message_iter_init_append(msg, &iter);

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &remote_desktop_info.session_path))
        goto out;

    if (!add_vardict(&iter, dict_array))
        goto out;

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &keycode))
        goto out;

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &state))
        goto out;

    if (dbus_connection_send(remote_desktop_info.connection, msg, NULL))
        status = TRUE;

out:
    if (msg)
        dbus_message_unref(msg);

    return status;
}

int
xdg_portal_init(void)
{
    if (!dbus_core_add_hook(&hook)) {
        ErrorF("XDG portal: failed to add D-Bus hook\n");
        return 0;
    }
    LogMessage(X_DEBUG, "XDG portal: initialized\n");

    return 1;
}

void
xdg_portal_fini(void)
{
    dbus_core_remove_hook(&hook);
}
