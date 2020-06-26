/*! \file libel/ezoption.c
 *  \brief eazy embedded client option
 *
 * Copyright (c) 2020 Skyhook, Inc.
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
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <sys/time.h>
#define __USE_POSIX199309
#define _POSIX_C_SOURCE 199309L
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include "libel.h"
#include "libez.h"

static XPS_Option_t options[XPS_MAX_OPTIONS] = { { 0 } };
static int num_options = 0;

/*! \brief find option in list
 *
 *  @param key uniq identifier for a new option
 *
 *  @returns pointer to option if found, NULL otherwise
 */
XPS_Option_t *XPS_find_option(char *key)
{
    int i;

    if (num_options == 0)
        return NULL;

    if (key == NULL || strlen(key) == 0)
        return NULL;

    for (i = 0; i < XPS_MAX_OPTIONS && options[i].key != NULL; i++) {
        if (strcmp(key, options[i].key) == 0)
            return &options[i];
    }
    return NULL;
}

/*! \brief define an option
 *
 *  @param key uniq identifier for a new option
 *  @param value of the new option
 *
 *  @returns XPS_STATUS_OK if option is set, error otherwize
 */
XPS_StatusCode_t XPS_set_option(char *key, char *value)
{
    XPS_Option_t *p;

    if (key == NULL || strlen(key) == 0)
        return XPS_STATUS_ERROR_INTERNAL;

    if ((p = XPS_find_option(key)) == NULL) {
        options[num_options].key = key;
        options[num_options].value = value;
        num_options++;
    } else {
        p->value = value;
    }

    return XPS_STATUS_OK;
}

/*! \brief retrieve an option
 *
 *  @param key uniq identifier for a new option
 *  @param value is saved here
 *
 *  @returns XPS_STATUS_OK if option is found, error otherwize
 */
XPS_StatusCode_t XPS_get_option(char *key, char **value)
{
    XPS_Option_t *p;

    if (key == NULL || strlen(key) == 0)
        return XPS_STATUS_ERROR_INTERNAL;

    if ((p = XPS_find_option(key)) == NULL) {
        return XPS_STATUS_ERROR_NOT_CONFIGURED;
    } else {
        if (value != NULL)
            *value = p->value;
    }

    return XPS_STATUS_OK;
}
