/*
 * Copyright © 2019-2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "steam-runtime-tools/json-report-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"

#include "steam-runtime-tools/container-internal.h"
#include "steam-runtime-tools/cpu-feature-internal.h"
#include "steam-runtime-tools/desktop-entry-internal.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/steam-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "steam-runtime-tools/virtualization-internal.h"

/**
 * _srt_architecture_can_run_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "can-run"
 *  property
 *
 * Returns: %TRUE if the provided @json_obj has the "can-run" member with a
 *  positive boolean value.
 */
gboolean
_srt_architecture_can_run_from_report (JsonObject *json_obj)
{
  g_return_val_if_fail (json_obj != NULL, FALSE);

  return json_object_get_boolean_member_with_default (json_obj, "can-run", FALSE);
}

/**
 * _srt_system_info_get_container_info_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "container"
 *  property
 * @host_path: (not nullable): Used to return the host path
 *
 * If the provided @json_obj doesn't have a "container" member,
 * %SRT_CONTAINER_TYPE_UNKNOWN will be returned.
 * If @json_obj has some elements that we can't parse, the returned
 * #SrtContainerType will be set to %SRT_CONTAINER_TYPE_UNKNOWN.
 *
 * Returns: The found container type
 */
SrtContainerInfo *
_srt_container_info_get_from_report (JsonObject *json_obj)
{
  g_autoptr(SrtOsInfo) host_os_info = NULL;
  JsonObject *json_sub_obj;
  JsonObject *json_host_obj = NULL;
  const gchar *flatpak_version = NULL;
  const gchar *host_path = NULL;
  int type = SRT_CONTAINER_TYPE_UNKNOWN;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "container"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "container");

      if (json_sub_obj == NULL)
        goto out;

      _srt_json_object_get_enum_member (json_sub_obj, "type",
                                        SRT_TYPE_CONTAINER_TYPE, &type);
      flatpak_version = json_object_get_string_member_with_default (json_sub_obj,
                                                                    "flatpak_version",
                                                                    NULL);

      if (json_object_has_member (json_sub_obj, "host"))
        {
          json_host_obj = json_object_get_object_member (json_sub_obj, "host");
          host_path = json_object_get_string_member_with_default (json_host_obj, "path",
                                                                  NULL);

          if (json_object_has_member (json_host_obj, "os-release"))
            host_os_info = _srt_os_info_new_from_report (json_host_obj);
        }
    }

out:
  return _srt_container_info_new (type,
                                  flatpak_version,
                                  host_path,
                                  host_os_info);
}

/**
 * _srt_feature_get_x86_flags_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "cpu-features"
 *  property
 * @known: (not nullable): Used to return the #SrtX86FeatureFlags that are
 *  known
 *
 * If the provided @json_obj doesn't have a "cpu-features" member, or it is
 * malformed, @known and the return value will be set to
 * %SRT_X86_FEATURE_NONE.
 * If @json_obj has some elements that we can't parse,
 * %SRT_X86_FEATURE_UNKNOWN will be added to the @known and, if they are of
 * positive value, to the return value too.
 *
 * Returns: the #SrtX86FeatureFlags that has been found
 */
SrtX86FeatureFlags
_srt_feature_get_x86_flags_from_report (JsonObject *json_obj,
                                        SrtX86FeatureFlags *known)
{
  JsonObject *json_sub_obj;

  g_return_val_if_fail (json_obj != NULL, SRT_X86_FEATURE_NONE);
  g_return_val_if_fail (known != NULL, SRT_X86_FEATURE_NONE);

  SrtX86FeatureFlags present = SRT_X86_FEATURE_NONE;
  *known = SRT_X86_FEATURE_NONE;

  if (json_object_has_member (json_obj, "cpu-features"))
    {
      GList *features_members = NULL;
      gboolean value = FALSE;
      json_sub_obj = json_object_get_object_member (json_obj, "cpu-features");

      if (json_sub_obj == NULL)
        goto out;

      features_members = json_object_get_members (json_sub_obj);

      for (GList *l = features_members; l != NULL; l = l->next)
        {
          if (!srt_add_flag_from_nick (SRT_TYPE_X86_FEATURE_FLAGS, l->data, known, NULL))
            *known |= SRT_X86_FEATURE_UNKNOWN;

          value = json_object_get_boolean_member (json_sub_obj, l->data);
          if (value)
            if (!srt_add_flag_from_nick (SRT_TYPE_X86_FEATURE_FLAGS, l->data, &present, NULL))
              present |= SRT_X86_FEATURE_UNKNOWN;
        }
      g_list_free (features_members);
    }

out:
  return present;
}

/**
 * _srt_get_steam_desktop_entries_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for "desktop-entries"
 *  property
 *
 * If the provided @json_obj doesn't have a "desktop-entries" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list with all the #SrtDesktopEntry that have been found, or %NULL
 *  if none has been found.
 */
GList *
_srt_get_steam_desktop_entries_from_json_report (JsonObject *json_obj)
{
  JsonArray *array;
  JsonObject *json_sub_obj;
  GList *desktop_entries = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "desktop-entries"))
    {
      array = json_object_get_array_member (json_obj, "desktop-entries");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint i = 0; i < length; i++)
        {
          const gchar *id = NULL;
          const gchar *commandline = NULL;
          const gchar *filename = NULL;
          gboolean is_default = FALSE;
          gboolean is_steam_handler = FALSE;
          json_sub_obj = json_array_get_object_element (array, i);
          id = json_object_get_string_member_with_default (json_sub_obj, "id", NULL);
          commandline = json_object_get_string_member_with_default (json_sub_obj, "commandline",
                                                                    NULL);
          filename = json_object_get_string_member_with_default (json_sub_obj, "filename", NULL);
          is_default = json_object_get_boolean_member_with_default (json_sub_obj,
                                                                    "default_steam_uri_handler",
                                                                    FALSE);
          is_steam_handler = json_object_get_boolean_member_with_default (json_sub_obj,
                                                                          "steam_uri_handler",
                                                                          FALSE);

          desktop_entries = g_list_prepend (desktop_entries, _srt_desktop_entry_new (id,
                                                                                     commandline,
                                                                                     filename,
                                                                                     is_default,
                                                                                     is_steam_handler));
        }
    }

out:
  return desktop_entries;
}

/**
 * _srt_library_get_issues_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "library-issues-summary" property
 *
 * If the provided @json_obj doesn't have a "library-issues-summary" member,
 * or it is malformed, %SRT_LIBRARY_ISSUES_UNKNOWN will be returned.
 * If @json_obj has some elements that we can't parse,
 * %SRT_LIBRARY_ISSUES_UNKNOWN will be added to the returned #SrtLibraryIssues.
 *
 * Returns: The #SrtLibraryIssues that has been found
 */
SrtLibraryIssues
_srt_library_get_issues_from_report (JsonObject *json_obj)
{
  g_return_val_if_fail (json_obj != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);

  return srt_get_flags_from_json_array (SRT_TYPE_LIBRARY_ISSUES,
                                        json_obj,
                                        "library-issues-summary",
                                        SRT_LIBRARY_ISSUES_UNKNOWN);
}

/**
 * _srt_os_info_new_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "os-release"
 *  property
 */
SrtOsInfo *
_srt_os_info_new_from_report (JsonObject *json_obj)
{
  g_autoptr(GHashTable) fields = NULL;
  g_autofree gchar *messages = NULL;
  JsonObject *json_sub_obj;
  JsonArray *array;
  const char *source_path = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  fields = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  if (json_object_has_member (json_obj, "os-release"))
    {
      gsize i;

      json_sub_obj = json_object_get_object_member (json_obj, "os-release");

      if (json_sub_obj == NULL)
        return _srt_os_info_new (NULL, "'os-release' is not a JSON object", NULL);

      if (json_object_has_member (json_sub_obj, "id_like"))
        {
          array = json_object_get_array_member (json_sub_obj, "id_like");
          /* We are expecting an array of OS IDs here */
          if (array == NULL)
            {
              g_debug ("'id_like' in 'os-release' is not an array as expected");
            }
          else
            {
              GString *str = g_string_new ("");
              guint length = json_array_get_length (array);

              for (i = 0; i < length; i++)
                {
                  const char *temp_id = json_array_get_string_element (array, i);

                  if (str->len > 0)
                    g_string_append_c (str, ' ');

                  g_string_append (str, temp_id);
                }

              g_hash_table_replace (fields, g_strdup ("ID_LIKE"),
                                    g_string_free (str, FALSE));
            }
        }

      for (i = 0; _srt_interesting_os_release_fields[i] != NULL; i++)
        {
          const char *member = _srt_interesting_os_release_fields[i];
          const char *value;

          /* id_like is handled specially, above */
          if (g_str_equal (member, "id_like"))
            continue;

          value = json_object_get_string_member_with_default (json_sub_obj,
                                                              member,
                                                              NULL);

          if (value != NULL)
            g_hash_table_replace (fields,
                                  g_ascii_strup (member, -1),
                                  g_strdup (value));
        }

      if (json_object_has_member (json_sub_obj, "fields"))
        {
          g_autoptr(GList) members = NULL;
          JsonObject *fields_obj;
          const char *value;

          fields_obj = json_object_get_object_member (json_sub_obj, "fields");

          if (fields_obj != NULL)
            members = json_object_get_members (fields_obj);

          for (GList *l = members; l != NULL; l = l->next)
            {
              value = json_object_get_string_member_with_default (fields_obj,
                                                                  l->data,
                                                                  NULL);

              if (value != NULL)
                g_hash_table_replace (fields,
                                      g_strdup (l->data),
                                      g_strdup (value));
            }
        }

      messages = _srt_json_object_dup_array_of_lines_member (json_sub_obj,
                                                             "messages");
      source_path = json_object_get_string_member_with_default (json_sub_obj,
                                                                "source_path",
                                                                NULL);
    }

  return _srt_os_info_new (fields, messages, source_path);
}

/*
 * _srt_runtime_get_issues_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "issues"
 *  property
 *
 * If the provided @json_obj doesn't have a "issues" member, or it is
 * malformed, %SRT_RUNTIME_ISSUES_UNKNOWN will be returned.
 * If @json_obj has some elements that we can't parse,
 * %SRT_RUNTIME_ISSUES_UNKNOWN will be added to the returned #SrtRuntimeIssues.
 *
 * Returns: The #SrtRuntimeIssues that has been found
 */
static SrtRuntimeIssues
_srt_runtime_get_issues_from_report (JsonObject *json_obj)
{
  g_return_val_if_fail (json_obj != NULL, SRT_RUNTIME_ISSUES_UNKNOWN);

  return srt_get_flags_from_json_array (SRT_TYPE_RUNTIME_ISSUES,
                                        json_obj,
                                        "issues",
                                        SRT_RUNTIME_ISSUES_UNKNOWN);
}

static gchar *
dup_json_string_member (JsonObject *obj,
                        const gchar *name)
{
  JsonNode *node = json_object_get_member (obj, name);

  if (node == NULL)
    return NULL;

  /* This returns NULL without error if it is a non-string */
  return json_node_dup_string (node);
}

static unsigned long
get_json_hex_member (JsonObject *obj,
                     const gchar *name)
{
  const char *s;

  s = _srt_json_object_get_string_member (obj, name);

  if (s == NULL)
    return 0;

  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;

  return strtoul (s, NULL, 16);
}

static JsonObject *
get_json_object_member (JsonObject *obj,
                        const char *name)
{
  JsonNode *node = json_object_get_member (obj, name);

  if (node != NULL && JSON_NODE_HOLDS_OBJECT (node))
    return json_node_get_object (node);

  return NULL;
}

static gchar *
dup_json_uevent (JsonObject *obj)
{
  return _srt_json_object_dup_array_of_lines_member (obj, "uevent");
}

/*
 * @buf: a mutable byte array
 * @string: a series of space- and/or comma-separated hexadecimal numbers
 *  representing bytes, each with an optional `0x` prefix
 *
 * Parse byte values from @string and append them to @buf.
 *
 * Returns: %TRUE if successful, %FALSE if invalid content is found
 */
static gboolean
read_hex_dump_string (GByteArray *buf,
                      const char *string)
{
  if (string == NULL)
    return FALSE;

  while (*string != '\0')
    {
      unsigned int scanned;
      unsigned char this_byte;
      int used;

      while (*string == ' ' || *string == ',')
        string++;

      if (*string == '\0')
        break;

      if (g_str_has_prefix (string, "0x"))
        string += 2;

      if (sscanf (string, "%x%n", &scanned, &used) == 1
          && used >= 1
          && used <= 2
          && scanned < 0x100)
        string += used;
      else
        return FALSE;

      this_byte = scanned;
      g_byte_array_append (buf, &this_byte, 1);
    }

  return TRUE;
}

/*
 * @obj: a JSON object
 * @name: a member name
 *
 * Look up `obj[name]` and attempt to parse it as a representation of an
 * opaque byte array: either a string with space-separated hexadecimal
 * numbers representing bytes, or an array of such strings.
 *
 * Returns: (transfer full): An array of bytes, or %NULL on a parsing error.
 */
static GBytes *
dup_json_hex_dump (JsonObject *obj,
                   const char *name)
{
  JsonNode *node = json_object_get_member (obj, name);
  g_autoptr(GByteArray) buf = g_byte_array_new ();

  if (node == NULL || JSON_NODE_HOLDS_NULL (node))
    return NULL;

  if (JSON_NODE_HOLDS_VALUE (node))
    {
      if (!read_hex_dump_string (buf, json_node_get_string (node)))
        return NULL;
    }
  else if (JSON_NODE_HOLDS_ARRAY (node))
    {
      JsonArray *array;
      guint length;

      array = json_node_get_array (node);

      if (array == NULL)
        return NULL;

      length = json_array_get_length (array);

      for (guint i = 0; i < length; i++)
        {
          JsonNode *inner_node = json_array_get_element (array, i);
          const gchar *element = json_node_get_string (inner_node);

          if (element == NULL
              || !read_hex_dump_string (buf, element))
            return NULL;
        }
    }

  return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static void
get_json_evdev_caps (JsonObject *obj,
                     const char *name,
                     unsigned long *longs,
                     size_t n_longs)
{
  g_autoptr(GBytes) bytes = NULL;
  size_t n_bytes = n_longs * sizeof (long);
  const void *data;
  size_t i;
  size_t len;

  bytes = dup_json_hex_dump (obj, name);

  if (bytes == NULL)
    return;

  data = g_bytes_get_data (bytes, &len);
  memset (longs, 0, n_bytes);
  memcpy (longs, data, MIN (len, n_bytes));

  for (i = 0; i < n_longs; i++)
    longs[i] = GULONG_FROM_LE (longs[i]);
}

SrtSimpleInputDevice *
_srt_simple_input_device_new_from_json (JsonObject *obj)
{
  SrtSimpleInputDevice *self = g_object_new (SRT_TYPE_SIMPLE_INPUT_DEVICE,
                                             NULL);
  JsonObject *sub;

  self->sys_path = dup_json_string_member (obj, "sys_path");
  self->dev_node = dup_json_string_member (obj, "dev_node");
  self->subsystem = dup_json_string_member (obj, "subsystem");
  self->bus_type  = get_json_hex_member (obj, "bus_type");
  self->vendor_id  = get_json_hex_member (obj, "vendor_id");
  self->product_id  = get_json_hex_member (obj, "product_id");
  self->version  = get_json_hex_member (obj, "version");

  self->iface_flags = srt_get_flags_from_json_array (SRT_TYPE_INPUT_DEVICE_INTERFACE_FLAGS,
                                                     obj,
                                                     "interface_flags",
                                                     SRT_INPUT_DEVICE_INTERFACE_FLAGS_NONE);
  self->type_flags = srt_get_flags_from_json_array (SRT_TYPE_INPUT_DEVICE_TYPE_FLAGS,
                                                    obj,
                                                    "type_flags",
                                                    SRT_INPUT_DEVICE_TYPE_FLAGS_NONE);

  if ((sub = get_json_object_member (obj, "evdev")) != NULL)
    {
      get_json_evdev_caps (sub, "raw_types", self->evdev_caps.ev,
                           G_N_ELEMENTS (self->evdev_caps.ev));
      get_json_evdev_caps (sub, "raw_abs", self->evdev_caps.abs,
                           G_N_ELEMENTS (self->evdev_caps.abs));
      get_json_evdev_caps (sub, "raw_rel", self->evdev_caps.rel,
                           G_N_ELEMENTS (self->evdev_caps.rel));
      get_json_evdev_caps (sub, "raw_keys", self->evdev_caps.keys,
                           G_N_ELEMENTS (self->evdev_caps.keys));
      get_json_evdev_caps (sub, "raw_input_properties",
                           self->evdev_caps.props,
                           G_N_ELEMENTS (self->evdev_caps.props));
    }

  self->udev_properties = _srt_json_object_dup_strv_member (obj, "udev_properties", NULL);
  self->uevent = dup_json_uevent (obj);

  if ((sub = get_json_object_member (obj, "hid_ancestor")) != NULL)
    {
      self->hid_ancestor.sys_path = dup_json_string_member (sub, "sys_path");
      self->hid_ancestor.name = dup_json_string_member (sub, "name");
      self->hid_ancestor.bus_type = get_json_hex_member (sub, "bus_type");
      self->hid_ancestor.vendor_id = get_json_hex_member (sub, "vendor_id");
      self->hid_ancestor.product_id = get_json_hex_member (sub, "product_id");
      self->hid_ancestor.uniq = dup_json_string_member (sub, "uniq");
      self->hid_ancestor.phys = dup_json_string_member (sub, "phys");
      self->hid_ancestor.uevent = dup_json_uevent (sub);
      self->hid_ancestor.report_descriptor = dup_json_hex_dump (sub, "report_descriptor");
    }

  if ((sub = get_json_object_member (obj, "input_ancestor")) != NULL)
    {
      self->input_ancestor.sys_path = dup_json_string_member (sub, "sys_path");
      self->input_ancestor.name = dup_json_string_member (sub, "name");
      self->input_ancestor.bus_type = get_json_hex_member (sub, "bus_type");
      self->input_ancestor.vendor_id = get_json_hex_member (sub, "vendor_id");
      self->input_ancestor.product_id = get_json_hex_member (sub, "product_id");
      self->input_ancestor.version = get_json_hex_member (sub, "version");
      self->input_ancestor.uniq = dup_json_string_member (sub, "uniq");
      self->input_ancestor.phys = dup_json_string_member (sub, "phys");
      self->input_ancestor.uevent = dup_json_uevent (sub);
    }

  if ((sub = get_json_object_member (obj, "usb_device_ancestor")) != NULL)
    {
      self->usb_device_ancestor.sys_path = dup_json_string_member (sub, "sys_path");
      self->usb_device_ancestor.vendor_id = get_json_hex_member (sub, "vendor_id");
      self->usb_device_ancestor.product_id = get_json_hex_member (sub, "product_id");
      self->usb_device_ancestor.device_version = get_json_hex_member (sub, "version");
      self->usb_device_ancestor.manufacturer = dup_json_string_member (sub, "manufacturer");
      self->usb_device_ancestor.product = dup_json_string_member (sub, "product");
      self->usb_device_ancestor.serial = dup_json_string_member (sub, "serial");
      self->usb_device_ancestor.uevent = dup_json_uevent (sub);
    }

  return self;
}

void
_srt_runtime_fill_from_report (SrtRuntime *self,
                               JsonObject *json_obj)
{
  self->issues = SRT_RUNTIME_ISSUES_UNKNOWN;

  if (json_object_has_member (json_obj, "runtime"))
    {
      JsonObject *json_sub_obj = json_object_get_object_member (json_obj, "runtime");

      g_clear_pointer (&self->path, g_free);
      self->path = g_strdup (json_object_get_string_member_with_default (json_sub_obj,
                                                                         "path",
                                                                         NULL));
      g_clear_pointer (&self->version, g_free);
      self->version = g_strdup (json_object_get_string_member_with_default (json_sub_obj,
                                                                            "version",
                                                                            NULL));
      self->issues = _srt_runtime_get_issues_from_report (json_sub_obj);
    }
}

/**
 * _srt_steam_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "steam-installation" property
 *
 * If the provided @json_obj doesn't have a "steam-installation" member,
 * #SrtSteamIssues of the returned #SrtSteam will be set to
 * %SRT_STEAM_ISSUES_UNKNOWN.
 *
 * Returns: (transfer full): a new #StrSteam object.
 */
SrtSteam *
_srt_steam_get_from_report (JsonObject *json_obj)
{
  JsonObject *json_sub_obj;
  JsonArray *array;
  SrtSteamIssues issues = SRT_STEAM_ISSUES_UNKNOWN;
  const gchar *install_path = NULL;
  const gchar *data_path = NULL;
  const gchar *bin32_path = NULL;
  const gchar *steamscript_path = NULL;
  const gchar *steamscript_version = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "steam-installation"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "steam-installation");

      if (json_sub_obj == NULL)
        goto out;

      if (json_object_has_member (json_sub_obj, "issues"))
        {
          issues = SRT_STEAM_ISSUES_NONE;
          array = json_object_get_array_member (json_sub_obj, "issues");

          /* We are expecting an array of issues here */
          if (array == NULL)
            {
              g_debug ("'issues' in 'steam-installation' is not an array as expected");
              issues |= SRT_STEAM_ISSUES_UNKNOWN;
            }
          else
            {
              for (guint i = 0; i < json_array_get_length (array); i++)
                {
                  const gchar *issue_string = json_array_get_string_element (array, i);
                  if (!srt_add_flag_from_nick (SRT_TYPE_STEAM_ISSUES, issue_string, &issues, NULL))
                    issues |= SRT_STEAM_ISSUES_UNKNOWN;
                }
            }
        }

      install_path = json_object_get_string_member_with_default (json_sub_obj, "path", NULL);
      data_path = json_object_get_string_member_with_default (json_sub_obj, "data_path", NULL);
      bin32_path = json_object_get_string_member_with_default (json_sub_obj, "bin32_path", NULL);
      steamscript_path = json_object_get_string_member_with_default (json_sub_obj,
                                                                     "steamscript_path",
                                                                     NULL);
      steamscript_version = json_object_get_string_member_with_default (json_sub_obj,
                                                                        "steamscript_version",
                                                                        NULL);
    }

out:
  return _srt_steam_new (issues,
                         install_path,
                         data_path,
                         bin32_path,
                         steamscript_path,
                         steamscript_version);
}

/**
 * _srt_system_info_get_virtualization_info_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for the
 *  "virtualization" property
 *
 * If the provided @json_obj doesn't have a "virtualization" member,
 * %SRT_VIRTUALIZATION_TYPE_UNKNOWN will be returned.
 *
 * Returns: Information about virtualization, hypervisor or emulation
 */
SrtVirtualizationInfo *
_srt_virtualization_info_get_from_report (JsonObject *json_obj)
{
  JsonObject *json_sub_obj;
  g_autoptr(SrtOsInfo) host_os_info = NULL;
  const char *host_path = NULL;
  const gchar *interpreter_root = NULL;
  int type = SRT_CONTAINER_TYPE_UNKNOWN;
  int host_machine = SRT_MACHINE_TYPE_UNKNOWN;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "virtualization"))
    {
      JsonObject *host_obj = NULL;

      json_sub_obj = json_object_get_object_member (json_obj, "virtualization");

      if (json_sub_obj == NULL)
        goto out;

      _srt_json_object_get_enum_member (json_sub_obj, "type",
                                        SRT_TYPE_VIRTUALIZATION_TYPE, &type);
      _srt_json_object_get_enum_member (json_sub_obj, "host-machine",
                                        SRT_TYPE_MACHINE_TYPE, &host_machine);
      interpreter_root = json_object_get_string_member_with_default (json_sub_obj,
                                                                     "interpreter-root",
                                                                     NULL);
      if (json_object_has_member (json_sub_obj, "host"))
        host_obj = json_object_get_object_member (json_sub_obj, "host");

      if (host_obj != NULL)
        {
          host_path = json_object_get_string_member_with_default (host_obj,
                                                                  "path",
                                                                  NULL);

          if (json_object_has_member (host_obj, "os-release"))
            host_os_info = _srt_os_info_new_from_report (host_obj);
        }
    }

out:
  return _srt_virtualization_info_new (host_machine, host_os_info, host_path,
                                       interpreter_root, type);
}
