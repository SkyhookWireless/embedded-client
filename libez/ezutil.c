/*! \file libel/utilities.c
 *  \brief eazy embedded client utilities
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
#define SKY_LIBEL
#include "libel.h"
#include "libez.h"
#include "ezutil.h"

/*! \brief returns a string which describes the meaning of error codes
 *
 *  @param err Error code for which to provide descriptive string
 *
 *  @return pointer to string or NULL if the code is invalid
 */
char *XPS_perror(XPS_StatusCode_t err)
{
    register char *str = NULL;
    switch (err) {
    case XPS_STATUS_OK:
        str = "No error";
        break;
    case XPS_STATUS_ERROR_NOT_CONFIGURED:
        str = "Essential options were not set";
        break;
    case XPS_STATUS_ERROR_BAD_BEACON_DATA:
        str = "Beacon data could not be processed";
        break;
    case XPS_STATUS_ERROR_NOT_AUTHORIZED:
        str = "Server reported problem with key";
        break;
    case XPS_STATUS_ERROR_SERVER_UNAVAILABLE:
        str = "Unable to reach server";
        break;
    case XPS_STATUS_ERROR_NETWORK_ERROR:
        str = "Incomplete communication with server";
        break;
    case XPS_STATUS_ERROR_LOCATION_CANNOT_BE_DETERMINED:
        str = "Server reported that location could not be determined";
        break;
    case XPS_STATUS_ERROR_INTERNAL:
        str = "Undefined internal error";
        break;
    default:
        str = "Unknown error";
    }
    return str;
}

/*! \brief formatted logging to user provided function
 *
 *  @param ctx workspace buffer
 *  @param level the log level of this msg
 *  @param fmt the msg
 *  @param ... variable arguments
 *
 *  @return 0 for success
 */
int XPS_log_error(char *fmt, ...)
{
    va_list ap;
    char buf[SKY_LOG_LENGTH];
    int ret;

    memset(buf, '\0', sizeof(buf));

    va_start(ap, fmt);
    ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    XPS_skylogger(SKY_LOG_LEVEL_ERROR, buf);
    va_end(ap);
    return ret;
}

int XPS_log_debug(char *fmt, ...)
{
    va_list ap;
    char buf[SKY_LOG_LENGTH];
    int ret;

    memset(buf, '\0', sizeof(buf));

    va_start(ap, fmt);
    ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    XPS_skylogger(SKY_LOG_LEVEL_DEBUG, buf);
    va_end(ap);
    return ret;
}

int XPS_log_warning(char *fmt, ...)
{
    va_list ap;
    char buf[SKY_LOG_LENGTH];
    int ret;

    memset(buf, '\0', sizeof(buf));

    va_start(ap, fmt);
    ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    XPS_skylogger(SKY_LOG_LEVEL_WARNING, buf);
    va_end(ap);
    return ret;
}

/*! \brief logging function
 *
 *  @param level log level of this message
 *  @param s this message
 *
 *  @returns 0 for success or negative number for error
 */
int XPS_skylogger(Sky_log_level_t level, char *s)
{
    switch (level) {
    case SKY_LOG_LEVEL_CRITICAL:
    case SKY_LOG_LEVEL_ERROR:
        printf("Error: %.*s\n", SKY_LOG_LENGTH, s);
        break;
    case SKY_LOG_LEVEL_WARNING:
    case SKY_LOG_LEVEL_DEBUG:
        printf("Debug: %.*s\n", SKY_LOG_LENGTH, s);
    }
    return 0;
}

uint32_t XPS_hex2bin(char *hexstr, uint32_t hexlen, uint8_t *result, uint32_t reslen)
{
    uint32_t i, j = 0, k = 0;

    for (i = 0; i < hexlen; i++) {
        uint8_t c = (uint8_t)hexstr[i];

        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'a' && c <= 'f')
            c = (uint8_t)((c - 'a') + 10);
        else if (c >= 'A' && c <= 'F')
            c = (uint8_t)((c - 'A') + 10);
        else
            continue;

        if (k++ & 0x01)
            result[j++] |= c;
        else
            result[j] = c << 4;

        if (j >= reslen)
            break;
    }

    return j;
}

int32_t XPS_bin2hex(char *buff, int32_t buff_len, uint8_t *data, int32_t data_len)
{
    const char *hex = "0123456789ABCDEF";

    char *p;
    int32_t i;

    if (buff_len < 2 * data_len)
        return -1;

    p = buff;

    for (i = 0; i < data_len; i++) {
        *p++ = hex[data[i] >> 4 & 0x0F];
        *p++ = hex[data[i] & 0x0F];
    }
    if (p - buff <= buff_len)
        *p++ = '\0';

    return 0;
}

XPS_LocationSource_t XPS_determine_source(Sky_loc_source_t s)
{
    return (
        s == SKY_LOCATION_SOURCE_HYBRID ?
            XPS_LOCATION_SOURCE_HYBRID :
            s == SKY_LOCATION_SOURCE_CELL ?
            XPS_LOCATION_SOURCE_CELL :
            s == SKY_LOCATION_SOURCE_WIFI ?
            XPS_LOCATION_SOURCE_WIFI :
            s == SKY_LOCATION_SOURCE_GNSS ? XPS_LOCATION_SOURCE_GNSS : XPS_LOCATION_SOURCE_UNKNOWN);
}

const char *XPS_determine_source_str(Sky_loc_source_t s)
{
    return (s == SKY_LOCATION_SOURCE_HYBRID ?
                "HYBRID" :
                s == SKY_LOCATION_SOURCE_CELL ?
                "CELL" :
                s == SKY_LOCATION_SOURCE_WIFI ?
                "WIFI" :
                s == SKY_LOCATION_SOURCE_GNSS ? "GNSS" :
                                                s == SKY_LOCATION_SOURCE_MAX ? "MAX" : "UNKNOWN");
}
