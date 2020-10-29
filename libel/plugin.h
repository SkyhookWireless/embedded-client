/* \brief Skyhook Embedded Library
*
* Copyright (c) 2019 Skyhook, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
*/
#ifndef SKY_PLUGIN_H
#define SKY_PLUGIN_H

#include <stdarg.h>

typedef Sky_status_t (*Sky_plugin_name_t)(Sky_ctx_t *ctx, char **pname);
typedef Sky_status_t (*Sky_plugin_equal_t)(
    Sky_ctx_t *ctx, Beacon_t *a, Beacon_t *b, Sky_beacon_property_t *prop);
typedef Sky_status_t (*Sky_plugin_add_to_cache_t)(Sky_ctx_t *ctx, Sky_location_t *loc);
typedef Sky_status_t (*Sky_plugin_match_cache_t)(Sky_ctx_t *ctx, int *idx);
typedef Sky_status_t (*Sky_plugin_remove_worst_t)(Sky_ctx_t *ctx);

/* Each plugin has table of operation functions */
/* Each registered plugin is added to the end of a linked list of tables */
typedef enum sky_operations {
    SKY_OP_NEXT = 0, /* must be 0 - link to next table */
    SKY_OP_NAME,
    SKY_OP_EQUAL,
    SKY_OP_REMOVE_WORST,
    SKY_OP_CACHE_MATCH,
    SKY_OP_ADD_TO_CACHE,
    SKY_OP_MAX, /* Add more operations before this */
} sky_operation_t;

typedef struct plugin_table {
    struct plugin_table *next;
    Sky_plugin_name_t name;
    Sky_plugin_equal_t equal;
    Sky_plugin_remove_worst_t remove_worst;
    Sky_plugin_match_cache_t match_cache;
    Sky_plugin_add_to_cache_t add_to_cache;
} Sky_plugin_table_t;

Sky_status_t sky_register_plugins(Sky_plugin_table_t **root);
Sky_status_t sky_plugin_init(Sky_plugin_table_t **root, Sky_plugin_table_t *table);
Sky_status_t sky_plugin_call(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, sky_operation_t n, ...);

Sky_status_t remove_beacon(Sky_ctx_t *ctx, int index);
Sky_status_t insert_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, Beacon_t *b, int *index);
int find_oldest(Sky_ctx_t *ctx);
int cell_changed(Sky_ctx_t *ctx, Sky_cacheline_t *cl);

#endif
