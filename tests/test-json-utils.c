/*
 * Copyright © 2019-2025 Collabora Ltd.
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

#include "tests/test-json-utils.h"

#include <glib.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"

/**
 * Returns a new string containing the input JSON but pretty-printed.
 */
gchar *
_srt_json_pretty (const char *unformatted)
{
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *node = NULL;  /* not owned */
  g_autoptr(JsonGenerator) generator = NULL;
  g_autoptr(GError) error = NULL;

  parser = json_parser_new ();
  json_parser_load_from_data (parser, unformatted, -1, &error);
  g_assert_no_error (error);
  node = json_parser_get_root (parser);
  g_assert_nonnull (node);
  generator = json_generator_new ();
  json_generator_set_root (generator, node);
  json_generator_set_pretty (generator, TRUE);
  return json_generator_to_data (generator, NULL);
}

/**
 * Returns the contents of the given JSON file, pretty-printed via
 * _srt_json_pretty().
 **/
gchar *
_srt_json_pretty_from_file (const char *path)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *contents = NULL;
  gsize len;

  g_file_get_contents (path,
                       &contents, &len, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (len, ==, strlen (contents));

  return _srt_json_pretty (contents);
}
