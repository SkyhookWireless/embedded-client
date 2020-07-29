/*! \file libel/utilities.c
 *  \brief utilities - Skyhook Embedded Library
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
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#define SKY_LIBEL 1
#include "libel.h"

#define MIN(a, b) ((a < b) ? a : b)

/*! \brief set sky_errno and return Sky_status
 *
 *  @param sky_errno sky_errno is the error code
 *  @param code the sky_errno_t code to return
 *
 *  @return Sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
Sky_status_t sky_return(Sky_errno_t *sky_errno, Sky_errno_t code)
{
    if (sky_errno != NULL)
        *sky_errno = code;
    return (code == SKY_ERROR_NONE) ? SKY_SUCCESS : SKY_ERROR;
}

/*! \brief validate the workspace buffer
 *
 *  @param ctx workspace buffer
 *
 *  @return true if workspace is valid, else false
 */
int validate_workspace(Sky_ctx_t *ctx)
{
    int i;

    if (ctx == NULL) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "NULL ctx");
        return false;
    }
    if (ctx->len > TOTAL_BEACONS) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Too many beacons");
        return false;
    }
    if (ctx->connected > TOTAL_BEACONS) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad connected value");
        return false;
    }
    if (ctx->header.crc32 == sky_crc32(&ctx->header.magic,
                                 (uint8_t *)&ctx->header.crc32 - (uint8_t *)&ctx->header.magic)) {
        for (i = 0; i < TOTAL_BEACONS; i++) {
            if (ctx->beacon[i].h.magic != BEACON_MAGIC || ctx->beacon[i].h.type > SKY_BEACON_MAX) {
                LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad beacon #%d of %d", i, TOTAL_BEACONS);
                return false;
            }
        }
    } else {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "CRC check failed");
        return false;
    }
    return true;
}

/*! \brief validate the cache buffer
 *
 *  @param c pointer to cache buffer
 *
 *  @return true if cache is valid, else false
 */
int validate_cache(Sky_cache_t *c, Sky_loggerfn_t logf)
{
    int i, j;

    if (c == NULL) {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_DEBUG, "Cache validation: NUL pointer");
#endif
        return false;
    }

    if (c->len != CACHE_SIZE) {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_DEBUG, "Cache validation failed: too big for CACHE_SIZE");
        return false;
#endif
    }
    if (c->newest >= CACHE_SIZE) {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_DEBUG, "Cache validation failed: newest too big for CACHE_SIZE");
#endif
        return false;
    }

    if (c->header.crc32 ==
        sky_crc32(&c->header.magic, (uint8_t *)&c->header.crc32 - (uint8_t *)&c->header.magic)) {
        for (i = 0; i < CACHE_SIZE; i++) {
            if (c->cacheline[i].len > TOTAL_BEACONS) {
#if SKY_DEBUG
                if (logf != NULL)
                    (*logf)(SKY_LOG_LEVEL_DEBUG,
                        "Cache validation failed: too many beacons for TOTAL_BEACONS");
#endif
                return false;
            }

            for (j = 0; j < TOTAL_BEACONS; j++) {
                if (c->cacheline[i].beacon[j].h.magic != BEACON_MAGIC) {
#if SKY_DEBUG
                    if (logf != NULL)
                        (*logf)(SKY_LOG_LEVEL_DEBUG, "Cache validation failed: Bad beacon info");
#endif
                    return false;
                }
                if (c->cacheline[i].beacon[j].h.type > SKY_BEACON_MAX) {
#if SKY_DEBUG
                    if (logf != NULL)
                        (*logf)(SKY_LOG_LEVEL_DEBUG, "Cache validation failed: Bad beacon type");
#endif
                    return false;
                }
            }
        }
    } else {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_DEBUG, "Cache validation failed: crc mismatch!");
#endif
        return false;
    }
    return true;
}

/*! \brief validate mac address
 *
 *  @param mac pointer to mac address
 *  @param ctx pointer to context
 *
 *  @return true if mac address not all zeros or ones
 */
int validate_mac(uint8_t mac[6], Sky_ctx_t *ctx)
{
    if (mac[0] == 0 || mac[0] == 0xff) {
        if (mac[0] == mac[1] && mac[0] == mac[2] && mac[0] == mac[3] && mac[0] == mac[4] &&
            mac[0] == mac[5]) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Invalid mac address");
            return false;
        }
    }

    return true;
}

#if SKY_DEBUG
/*! \brief basename return pointer to the basename of path or path
 *
 *  @param path pathname of file
 *
 *  @return pointer to basename or whole path
 */
const char *sky_basename(const char *path)
{
    const char *p = strrchr(path, '/');

    if (p == NULL)
        return path;
    else
        return p + 1;
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

int logfmt(
    const char *file, const char *function, Sky_ctx_t *ctx, Sky_log_level_t level, char *fmt, ...)
{
    va_list ap;
    char buf[SKY_LOG_LENGTH];
    int ret, n;
    if (level > ctx->min_level || function == NULL)
        return -1;
    memset(buf, '\0', sizeof(buf));
    // Print log-line prefix ("<source file>:<function name>")
    n = snprintf(buf, sizeof(buf), "%.20s:%.20s() ", sky_basename(file), function);

    va_start(ap, fmt);
    ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    (*ctx->logf)(level, buf);
    va_end(ap);
    return ret;
}
#endif

/*! \brief dump maximum number of bytes of the given buffer in hex on one line
 *
 *  @param file the file name where LOG_BUFFER was invoked
 *  @param function the function name where LOG_BUFFER was invoked
 *  @param ctx workspace buffer
 *  @param level the log level of this msg
 *  @param buffer where to start dumping the next line
 *  @param bufsize remaining size of the buffer in bytes
 *  @param buf_offset byte index of progress through the current buffer
 *
 *  @returns number of bytes dumped, or negitive number on error
 */
int dump_hex16(const char *file, const char *function, Sky_ctx_t *ctx, Sky_log_level_t level,
    void *buffer, uint32_t bufsize, int buf_offset)
{
    int pb = 0;
#if SKY_DEBUG
    char buf[SKY_LOG_LENGTH];
    uint8_t *b = (uint8_t *)buffer;
    int n, N;
    if (level > ctx->min_level || function == NULL || buffer == NULL || bufsize <= 0)
        return -1;
    memset(buf, '\0', sizeof(buf));
    // Print log-line prefix ("<source file>:<function name> <buf offset>:")
    n = snprintf(buf, sizeof(buf), "%.20s:%.20s() %07X:", sky_basename(file), function, buf_offset);

    // Calculate number of characters required to print 16 bytes
    N = n + (16 * 3); /* 16 bytes per line, 3 bytes per byte (' XX') */
    // if width of log line (SKY_LOG_LENGTH) too short 16 bytes, just print those that fit
    for (pb = 0; n < MIN(SKY_LOG_LENGTH - 4, N);) {
        if (pb < bufsize)
            n += sprintf(&buf[n], " %02X", b[pb++]);
        else
            break;
    }
    (*ctx->logf)(level, buf);
#endif
    return pb;
}

/*! \brief dump all bytes of the given buffer in hex
 *
 *  @param buf pointer to the buffer
 *  @param bufsize size of the buffer in bytes
 *
 *  @returns number of bytes dumped
 */
int log_buffer(const char *file, const char *function, Sky_ctx_t *ctx, Sky_log_level_t level,
    void *buffer, uint32_t bufsize)
{
    int buf_offset = 0;
#if SKY_DEBUG
    int i, n = bufsize;
    uint8_t *p = buffer;
    /* try to print 16 bytes per line till all dumped */
    while ((i = dump_hex16(
                file, function, ctx, level, (void *)(p + (bufsize - n)), n, buf_offset)) > 0) {
        n -= i;
        buf_offset += i;
    }
#endif
    return buf_offset;
}

/*! \brief dump the beacons in the workspace
 *
 *  @param ctx workspace pointer
 *
 *  @returns 0 for success or negative number for error
 */
void dump_workspace(Sky_ctx_t *ctx)
{
#if SKY_DEBUG
    int i;

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "WorkSpace: Got %d beacons, WiFi %d, connected %d", ctx->len,
        ctx->ap_len, ctx->connected)
    for (i = 0; i < ctx->len; i++) {
        switch (ctx->beacon[i].h.type) {
        case SKY_BEACON_AP:
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                " Beacon %-2d: Age: %d Type: WiFi, %sMAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %d, freq: %d",
                i, ctx->beacon[i].h.age, ctx->beacon[i].ap.in_cache ? "cached " : "       ",
                ctx->beacon[i].ap.mac[0], ctx->beacon[i].ap.mac[1], ctx->beacon[i].ap.mac[2],
                ctx->beacon[i].ap.mac[3], ctx->beacon[i].ap.mac[4], ctx->beacon[i].ap.mac[5],
                ctx->beacon[i].h.rssi, ctx->beacon[i].ap.freq)
            break;
        case SKY_BEACON_CDMA:
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                " Beacon %-2d: Age: %d Type: CDMA, sid: %u, nid: %u, bsid: %u, rssi: %d", i,
                ctx->beacon[i].h.age, ctx->beacon[i].cell.id2, ctx->beacon[i].cell.id3,
                ctx->beacon[i].cell.id4, ctx->beacon[i].h.rssi)
            break;
        case SKY_BEACON_GSM:
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                " Beacon %-2d: Age: %d Type: GSM, lac: %u, ui: %u, mcc: %u, mnc: %u, rssi: %d", i,
                ctx->beacon[i].h.age, ctx->beacon[i].cell.id3, ctx->beacon[i].cell.id4,
                ctx->beacon[i].cell.id1, ctx->beacon[i].cell.id2, ctx->beacon[i].h.rssi)
            break;
        case SKY_BEACON_LTE:
            if (ctx->beacon[i].cell.id1 == SKY_UNKNOWN_ID1 &&
                ctx->beacon[i].cell.id2 == SKY_UNKNOWN_ID2 &&
                ctx->beacon[i].cell.id4 == SKY_UNKNOWN_ID4)
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: Age: %d Type: LTE-NMR, pci: %u, earfcn: %u, rssi: %d", i,
                    ctx->beacon[i].h.age, ctx->beacon[i].cell.id5, ctx->beacon[i].cell.freq,
                    ctx->beacon[i].h.rssi)
            else
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: Age: %d Type: LTE, e-cellid: %u, mcc: %u, mnc: %u, tac: %u, pci: %u, earfcn: %u, rssi: %d",
                    i, ctx->beacon[i].h.age, ctx->beacon[i].cell.id4, ctx->beacon[i].cell.id1,
                    ctx->beacon[i].cell.id2, ctx->beacon[i].cell.id3, ctx->beacon[i].cell.id5,
                    ctx->beacon[i].cell.freq, ctx->beacon[i].h.rssi)
            break;
        case SKY_BEACON_NBIOT:
            if (ctx->beacon[i].cell.id1 == SKY_UNKNOWN_ID1 &&
                ctx->beacon[i].cell.id2 == SKY_UNKNOWN_ID2 &&
                ctx->beacon[i].cell.id4 == SKY_UNKNOWN_ID4)
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: Age: %d Type: NB-IoT-NMR, ncid: %u, earfcn: %u, rssi: %d", i,
                    ctx->beacon[i].h.age, ctx->beacon[i].cell.id5, ctx->beacon[i].cell.freq,
                    ctx->beacon[i].h.rssi)
            else
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: Age: %d Type: NB-IoT, mcc: %u, mnc: %u, e_cellid: %u, tac: %u, ncid: %u, earfcn: %u, rssi: %d",
                    i, ctx->beacon[i].h.age, ctx->beacon[i].cell.id1, ctx->beacon[i].cell.id2,
                    ctx->beacon[i].cell.id4, ctx->beacon[i].cell.id3, ctx->beacon[i].cell.id5,
                    ctx->beacon[i].cell.freq, ctx->beacon[i].h.rssi)
            break;
        case SKY_BEACON_UMTS:
            if (ctx->beacon[i].cell.id1 == SKY_UNKNOWN_ID1 &&
                ctx->beacon[i].cell.id2 == SKY_UNKNOWN_ID2 &&
                ctx->beacon[i].cell.id4 == SKY_UNKNOWN_ID4)
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: Age: %d Type: UMTS-NMR, psc: %u, uarfcn: %u, rssi: %d", i,
                    ctx->beacon[i].h.age, ctx->beacon[i].cell.id5, ctx->beacon[i].cell.freq,
                    ctx->beacon[i].h.rssi)
            else
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: Age: %d Type: UMTS, lac: %u, ucid: %u, mcc: %u, mnc: %u, psc: %u, uarfcn: %u, rssi: %d",
                    i, ctx->beacon[i].h.age, ctx->beacon[i].cell.id3, ctx->beacon[i].cell.id4,
                    ctx->beacon[i].cell.id1, ctx->beacon[i].cell.id2, ctx->beacon[i].cell.id5,
                    ctx->beacon[i].cell.freq, ctx->beacon[i].h.rssi)
            break;
        case SKY_BEACON_NR:
            if (ctx->beacon[i].cell.id1 == SKY_UNKNOWN_ID1 &&
                ctx->beacon[i].cell.id2 == SKY_UNKNOWN_ID2 &&
                ctx->beacon[i].cell.id4 == SKY_UNKNOWN_ID4)
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: AGE: %d Type: NR-NMR, pci: %u, nrarfcn: %u, rssi: %d", i,
                    ctx->beacon[i].h.age, ctx->beacon[i].cell.id5, ctx->beacon[i].cell.freq,
                    ctx->beacon[i].h.rssi)
            else
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    " Beacon %-2d: Age: %d Type: NR, mcc: %u, mnc: %u, nci: %llu, tac: %u, pci: %u, earfcn: %u, rssi: %d",
                    i, ctx->beacon[i].h.age, ctx->beacon[i].cell.id1, ctx->beacon[i].cell.id2,
                    ctx->beacon[i].cell.id4, ctx->beacon[i].cell.id3, ctx->beacon[i].cell.id5,
                    ctx->beacon[i].cell.freq, ctx->beacon[i].h.rssi)
            break;
        default:
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon %-2d: Type: Unknown", i)
            break;
        }
    }
    if (CONFIG(ctx->cache, last_config_time) == 0) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "Config: Total Beacons:%d Max AP:%d Thresholds:%d(Match) %d(Age) %d(Beacons) %d(RSSI) Update:Pending",
            CONFIG(ctx->cache, total_beacons), CONFIG(ctx->cache, max_ap_beacons),
            CONFIG(ctx->cache, cache_match_threshold), CONFIG(ctx->cache, cache_age_threshold),
            CONFIG(ctx->cache, cache_beacon_threshold),
            CONFIG(ctx->cache, cache_neg_rssi_threshold),
            ctx->header.time - CONFIG(ctx->cache, last_config_time))
    } else {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "Config: Total Beacons:%d Max AP Beacons:%d Thresholds:%d(Match) %d(Age) %d(Beacons) %d(RSSI) Update:%d Sec ago",
            CONFIG(ctx->cache, total_beacons), CONFIG(ctx->cache, max_ap_beacons),
            CONFIG(ctx->cache, cache_match_threshold), CONFIG(ctx->cache, cache_age_threshold),
            CONFIG(ctx->cache, cache_beacon_threshold),
            CONFIG(ctx->cache, cache_neg_rssi_threshold),
            (int)((*ctx->gettime)(NULL)-CONFIG(ctx->cache, last_config_time)))
    }
#endif
}

/*! \brief dump the beacons in the cache
 *
 *  @param ctx workspace pointer
 *
 *  @returns 0 for success or negative number for error
 */
void dump_cache(Sky_ctx_t *ctx)
{
#if SKY_DEBUG
    int i, j;
    Sky_cacheline_t *c;
    Beacon_t *b;

    for (i = 0; i < CACHE_SIZE; i++) {
        c = &ctx->cache->cacheline[i];
        if (c->len == 0 || c->time == 0) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d of %d - empty len:%d ap_len:%d time:%u", i,
                ctx->cache->len, c->len, c->ap_len, c->time)
        } else {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d of %d%s GPS:%d.%06d,%d.%06d,%d", i,
                ctx->cache->len, ctx->cache->newest == i ? "<-newest" : "", (int)c->loc.lat,
                (int)fabs(round(1000000 * (c->loc.lat - (int)c->loc.lat))), (int)c->loc.lon,
                (int)fabs(round(1000000 * (c->loc.lon - (int)c->loc.lon))), c->loc.hpe)
            for (j = 0; j < c->len; j++) {
                b = &c->beacon[j];
                switch (b->h.type) {
                case SKY_BEACON_AP:
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                        " Beacon %-2d:%-2d: Age: %d Type: WiFi, MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %d, freq %d",
                        i, j, b->h.age, b->ap.mac[0], b->ap.mac[1], b->ap.mac[2], b->ap.mac[3],
                        b->ap.mac[4], b->ap.mac[5], b->h.rssi, b->ap.freq)
                    break;
                case SKY_BEACON_CDMA:
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                        " Beacon %-2d:%-2d: Age: %d Type: CDMA, sid: %u, nid: %u, bsid: %u, rssi: %d",
                        i, j, b->h.age, b->cell.id2, b->cell.id3, b->cell.id4, b->h.rssi)
                    break;
                case SKY_BEACON_GSM:
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                        " Beacon %-2d:%-2d: Age: %d Type: GSM, lac: %u, ui: %u, mcc: %u, mnc: %u, rssi: %d",
                        i, j, b->h.age, b->cell.id3, b->cell.id4, b->cell.id1, b->cell.id2,
                        b->h.rssi)
                    break;
                case SKY_BEACON_LTE:
                    if (b->cell.id1 == SKY_UNKNOWN_ID1 && b->cell.id2 == SKY_UNKNOWN_ID2 &&
                        b->cell.id4 == SKY_UNKNOWN_ID4)
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: LTE-NMR, pci: %u, earfcn: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id5, b->cell.freq, b->h.rssi)
                    else
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: LTE, e-cellid: %u, mcc: %u, mnc: %u, tac: %u, pci: %u, earfcn: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id4, b->cell.id1, b->cell.id2, b->cell.id3,
                            b->cell.id5, b->cell.freq, b->h.rssi)
                    break;
                case SKY_BEACON_NBIOT:
                    if (b->cell.id1 == SKY_UNKNOWN_ID1 && b->cell.id2 == SKY_UNKNOWN_ID2 &&
                        b->cell.id4 == SKY_UNKNOWN_ID4)
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: NB-IoT-NMR, ncid: %u, earfcn: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id5, b->cell.freq, b->h.rssi)
                    else
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: NB-IoT, mcc: %u, mnc: %u, e_cellid: %u, tac: %u, ncid: %u, earfcn: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id1, b->cell.id2, b->cell.id4, b->cell.id3,
                            b->cell.id5, b->cell.freq, b->h.rssi)
                    break;
                case SKY_BEACON_UMTS:
                    if (b->cell.id1 == SKY_UNKNOWN_ID1 && b->cell.id2 == SKY_UNKNOWN_ID2 &&
                        b->cell.id4 == SKY_UNKNOWN_ID4)
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: UMTS-NMR, psc: %u, uarfcn: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id5, b->cell.freq, b->h.rssi)
                    else
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: UMTS, lac: %u, ucid: %u, mcc: %u, mnc: %u, psc: %u, uarfcn: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id3, b->cell.id4, b->cell.id1, b->cell.id2,
                            b->cell.id5, b->cell.freq, b->h.rssi)
                    break;
                case SKY_BEACON_NR:
                    if (b->cell.id1 == SKY_UNKNOWN_ID1 && b->cell.id2 == SKY_UNKNOWN_ID2 &&
                        b->cell.id4 == SKY_UNKNOWN_ID4)
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: NR-NMR, pci: %u, cell: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id5, b->cell.freq, b->h.rssi)
                    else
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                            " Beacon %-2d:%-2d: Age: %d Type: NR, mcc: %u, mnc: %u, nci: %llu, tac: %u, pci: %u, nrarfcn: %u, rssi: %d",
                            i, j, b->h.age, b->cell.id1, b->cell.id2, b->cell.id4, b->cell.id3,
                            b->cell.id5, b->cell.freq, b->h.rssi)
                    break;
                }
            }
        }
    }
#endif
}

/*! \brief set dynamic config parameter defaults
 *
 *  @param cache buffer
 *
 *  @return void
 */
void config_defaults(Sky_cache_t *c)
{
    if (CONFIG(c, total_beacons) == 0)
        CONFIG(c, total_beacons) = TOTAL_BEACONS;
    if (CONFIG(c, max_ap_beacons) == 0)
        CONFIG(c, max_ap_beacons) = MAX_AP_BEACONS;
    if (CONFIG(c, cache_match_threshold) == 0)
        CONFIG(c, cache_match_threshold) = CACHE_MATCH_THRESHOLD;
    if (CONFIG(c, cache_age_threshold) == 0)
        CONFIG(c, cache_age_threshold) = CACHE_AGE_THRESHOLD;
    if (CONFIG(c, cache_beacon_threshold) == 0)
        CONFIG(c, cache_beacon_threshold) = CACHE_BEACON_THRESHOLD;
    if (CONFIG(c, cache_neg_rssi_threshold) == 0)
        CONFIG(c, cache_neg_rssi_threshold) = CACHE_RSSI_THRESHOLD;
    /* Add new config parameters here */
}

/*! \brief field extraction for dynamic use of Nanopb (ctx partner_id)
 *
 *  @param ctx workspace buffer
 *
 *  @return partner_id
 */
uint32_t get_ctx_partner_id(Sky_ctx_t *ctx)
{
    return ctx->cache->sky_partner_id;
}

/*! \brief field extraction for dynamic use of Nanopb (ctx sky_aes_key)
 *
 *  @param ctx workspace buffer
 *
 *  @return sky_aes_key
 */
uint8_t *get_ctx_aes_key(Sky_ctx_t *ctx)
{
    return ctx->cache->sky_aes_key;
}

/*! \brief field extraction for dynamic use of Nanopb (ctx sky_device_id)
 *
 *  @param ctx workspace buffer
 *
 *  @return sky_device_id
 */
uint8_t *get_ctx_device_id(Sky_ctx_t *ctx)
{
    return ctx->cache->sky_device_id;
}

/*! \brief field extraction for dynamic use of Nanopb (ctx sky_id_len)
 *
 *  @param ctx workspace buffer
 *
 *  @return sky_id_len
 */
uint32_t get_ctx_id_length(Sky_ctx_t *ctx)
{
    return ctx->cache->sky_id_len;
}

/*! \brief field extraction for dynamic use of Nanopb (ctx logf)
 *
 *  @param ctx workspace buffer
 *
 *  @return logf
 */
Sky_loggerfn_t get_ctx_logf(Sky_ctx_t *ctx)
{
    return ctx->logf;
}

/*! \brief field extraction for dynamic use of Nanopb (ctx sky_id_len)
 *
 *  @param ctx workspace buffer
 *
 *  @return sky_id_len
 */
Sky_randfn_t get_ctx_rand_bytes(Sky_ctx_t *ctx)
{
    return ctx->rand_bytes;
}

/*! \brief field extraction for dynamic use of Nanopb (count beacons)
 *
 *  @param ctx workspace buffer
 *  @param t type of beacon to count
 *
 *  @return number of beacons of the specified type
 */
int32_t get_num_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t)
{
    int i, b = 0;

    if (ctx == NULL || t > SKY_BEACON_MAX) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    if (t == SKY_BEACON_AP) {
        return ctx->ap_len;
    } else {
        for (i = ctx->ap_len, b = 0; i < ctx->len; i++) {
            if (ctx->beacon[i].h.type == t)
                b++;
            if (b && ctx->beacon[i].h.type != t)
                break; /* End of beacons of this type */
        }
    }
    return b;
}

/*! \brief Return the total number of scanned cells (serving, neighbor, or otherwise)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of cells
 */
int32_t get_num_cells(Sky_ctx_t *ctx)
{
    int i, b = 0;

    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }

    for (i = ctx->ap_len, b = 0; i < ctx->len; i++) {
        if (ctx->beacon[i].h.type >= SKY_BEACON_FIRST_CELL_TYPE &&
            ctx->beacon[i].h.type <= SKY_BEACON_LAST_CELL_TYPE)
            b++;
    }

    return b;
}

/*! \brief field extraction for dynamic use of Nanopb (base of beacon type)
 *
 *  @param ctx workspace buffer
 *  @param t type of beacon to find
 *
 *  @return first beacon of the specified type
 */
int get_base_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t)
{
    int i = 0;

    if (ctx == NULL || t > SKY_BEACON_MAX) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    if (t == SKY_BEACON_AP) {
        if (ctx->beacon[0].h.type == t)
            return i;
    } else {
        for (i = ctx->ap_len; i < ctx->len; i++) {
            if (ctx->beacon[i].h.type == t)
                return i;
        }
    }
    return -1;
}

/*! \brief field extraction for dynamic use of Nanopb (num AP)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of AP beacons
 */
int32_t get_num_aps(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->ap_len;
}

/*! \brief field extraction for dynamic use of Nanopb (AP/MAC)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mac info
 */
uint8_t *get_ap_mac(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > ctx->ap_len) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[idx].ap.mac;
}

/*! \brief field extraction for dynamic use of Nanopb (AP/freq)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon channel info
 */
int64_t get_ap_freq(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > ctx->ap_len) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[idx].ap.freq;
}

/*! \brief field extraction for dynamic use of Nanopb (AP/rssi)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon rssi info
 */
int64_t get_ap_rssi(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > ctx->ap_len) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[idx].h.rssi;
}

/*! \brief field extraction for dynamic use of Nanopb (AP/is_connected)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon is_connected info
 */
bool get_ap_is_connected(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > ctx->ap_len) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return idx == ctx->connected;
}

/*! \brief field extraction for dynamic use of Nanopb (AP/timestamp)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon timestamp info
 */
int64_t get_ap_age(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > ctx->ap_len) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[idx].h.age;
}
#if 0
/*! \brief field extraction for dynamic use of Nanopb (num gsm)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of gsm beacons
 */
int32_t get_num_gsm(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return get_num_beacons(ctx, SKY_BEACON_GSM);
}

/*! \brief field extraction for dynamic use of Nanopb (gsm/ci)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon ci info
 */
int64_t get_gsm_ci(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_gsm(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_GSM) + idx].gsm.ci;
}

/*! \brief field extraction for dynamic use of Nanopb (gsm/mcc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mcc info
 */
int64_t get_gsm_mcc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_gsm(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_GSM) + idx].gsm.mcc;
}

/*! \brief field extraction for dynamic use of Nanopb (gsm/mnc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mnc info
 */
int64_t get_gsm_mnc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_gsm(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_GSM) + idx].gsm.mnc;
}

/*! \brief field extraction for dynamic use of Nanopb (gsm/lac)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon lac info
 */
int64_t get_gsm_lac(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_gsm(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_GSM) + idx].gsm.lac;
}

/*! \brief field extraction for dynamic use of Nanopb (gsm/rssi)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon rssi info
 */
int64_t get_gsm_rssi(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_gsm(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_GSM) + idx].gsm.rssi;
}

/*! \brief field extraction for dynamic use of Nanopb (gsm/is_connected)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon is_connected info
 */
bool get_gsm_is_connected(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_gsm(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->connected == get_base_beacons(ctx, SKY_BEACON_GSM) + idx;
}

/*! \brief field extraction for dynamic use of Nanopb (gsm/timestamp)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon timestamp info
 */
int64_t get_gsm_age(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_gsm(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_GSM) + idx].gsm.age;
}

/*! \brief field extraction for dynamic use of Nanopb (num nbiot)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of nbiot beacons
 */
int32_t get_num_nbiot(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return get_num_beacons(ctx, SKY_BEACON_NBIOT);
}

/*! \brief field extraction for dynamic use of Nanopb (nbiot/mcc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mcc info
 */
int64_t get_nbiot_mcc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_nbiot(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_NBIOT) + idx].nbiot.mcc;
}

/*! \brief field extraction for dynamic use of Nanopb (nbiot/mnc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mnc info
 */
int64_t get_nbiot_mnc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_nbiot(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_NBIOT) + idx].nbiot.mnc;
}

/*! \brief field extraction for dynamic use of Nanopb (nbiot/e_cellid)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon e cellid info
 */
int64_t get_nbiot_ecellid(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_nbiot(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_NBIOT) + idx].nbiot.e_cellid;
}

/*! \brief field extraction for dynamic use of Nanopb (nbiot/tac)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon tac info
 */
int64_t get_nbiot_tac(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_nbiot(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_NBIOT) + idx].nbiot.tac;
}

/*! \brief field extraction for dynamic use of Nanopb (nbiot/rssi)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon rssi info
 */
int64_t get_nbiot_rssi(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_nbiot(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_NBIOT) + idx].nbiot.rssi;
}

/*! \brief field extraction for dynamic use of Nanopb (nbiot/is_connected)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon is_connected info
 */
bool get_nbiot_is_connected(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_nbiot(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->connected == get_base_beacons(ctx, SKY_BEACON_NBIOT) + idx;
}

/*! \brief field extraction for dynamic use of Nanopb (nbiot/timestamp)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon timestamp info
 */
int64_t get_nbiot_age(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_nbiot(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_NBIOT) + idx].nbiot.age;
}

/*! \brief field extraction for dynamic use of Nanopb (num lte)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of lte beacons
 */
int32_t get_num_lte(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return get_num_beacons(ctx, SKY_BEACON_LTE);
}

/*! \brief field extraction for dynamic use of Nanopb (lte/mcc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mcc info
 */
int64_t get_lte_mcc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_lte(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_LTE) + idx].lte.mcc;
}

/*! \brief field extraction for dynamic use of Nanopb (lte/mnc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mnc info
 */
int64_t get_lte_mnc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_lte(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_LTE) + idx].lte.mnc;
}

/*! \brief field extraction for dynamic use of Nanopb (lte/e_cellid)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon e cellid info
 */
int64_t get_lte_e_cellid(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_lte(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_LTE) + idx].lte.e_cellid;
}

/*! \brief field extraction for dynamic use of Nanopb (lte/tac)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon tac info
 */
int64_t get_lte_tac(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_lte(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_LTE) + idx].lte.tac;
}

/*! \brief field extraction for dynamic use of Nanopb (lte/rssi)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon rssi info
 */
int64_t get_lte_rssi(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_lte(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_LTE) + idx].lte.rssi;
}

/*! \brief field extraction for dynamic use of Nanopb (lte/is_connected)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon is_connected info
 */
bool get_lte_is_connected(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_lte(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->connected == get_base_beacons(ctx, SKY_BEACON_LTE) + idx;
}

/*! \brief field extraction for dynamic use of Nanopb (lte/timestamp)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon timestamp info
 */
int64_t get_lte_age(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_lte(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_LTE) + idx].lte.age;
}

/*! \brief field extraction for dynamic use of Nanopb (num cdma)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of cdma beacons
 */
int32_t get_num_cdma(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return get_num_beacons(ctx, SKY_BEACON_CDMA);
}

/*! \brief field extraction for dynamic use of Nanopb (cdma/sid)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon sid info
 */
int64_t get_cdma_sid(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_cdma(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_CDMA) + idx].cdma.sid;
}

/*! \brief field extraction for dynamic use of Nanopb (cdma/nid)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon nid info
 */
int64_t get_cdma_nid(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_cdma(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_CDMA) + idx].cdma.nid;
}

/*! \brief field extraction for dynamic use of Nanopb (cdma/bsid)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon bsid info
 */
int64_t get_cdma_bsid(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_cdma(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_CDMA) + idx].cdma.bsid;
}

/*! \brief field extraction for dynamic use of Nanopb (cdma/rssi)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon rssi info
 */
int64_t get_cdma_rssi(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_cdma(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_CDMA) + idx].cdma.rssi;
}

/*! \brief field extraction for dynamic use of Nanopb (cdma/is_connected)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon is_connected info
 */
bool get_cdma_is_connected(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_cdma(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->connected == get_base_beacons(ctx, SKY_BEACON_CDMA) + idx;
}

/*! \brief field extraction for dynamic use of Nanopb (cdma/timestamp)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon timestamp info
 */
int64_t get_cdma_age(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_cdma(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_CDMA) + idx].cdma.age;
}

/*! \brief field extraction for dynamic use of Nanopb (num umts)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of umts beacons
 */
int32_t get_num_umts(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return get_num_beacons(ctx, SKY_BEACON_UMTS);
}

/*! \brief field extraction for dynamic use of Nanopb (umts/lac)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon lac info
 */
int64_t get_umts_lac(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_umts(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_UMTS) + idx].umts.lac;
}

/*! \brief field extraction for dynamic use of Nanopb (umts/ucid)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon ucid info
 */
int64_t get_umts_ucid(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_umts(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_UMTS) + idx].umts.ucid;
}

/*! \brief field extraction for dynamic use of Nanopb (umts/mcc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mcc info
 */
int64_t get_umts_mcc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_umts(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_UMTS) + idx].umts.mcc;
}

/*! \brief field extraction for dynamic use of Nanopb (umts/mnc)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon mnc info
 */
int64_t get_umts_mnc(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_umts(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_UMTS) + idx].umts.mnc;
}

/*! \brief field extraction for dynamic use of Nanopb (umts/rssi)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon rssi info
 */
int64_t get_umts_rssi(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_umts(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_UMTS) + idx].umts.rssi;
}

/*! \brief field extraction for dynamic use of Nanopb (umts/is_connected)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon is_connected info
 */
bool get_umts_is_connected(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_umts(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return ctx->connected == get_base_beacons(ctx, SKY_BEACON_UMTS) + idx;
}

/*! \brief field extraction for dynamic use of Nanopb (umts/timestamp)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return beacon timestamp info
 */
int64_t get_umts_age(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || idx > (ctx->len - get_num_umts(ctx))) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_UMTS) + idx].umts.age;
}
#endif
/*! \brief field extraction for dynamic use of Nanopb (num gnss)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of gnss
 */
int32_t get_num_gnss(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param")
        return 0;
    }
    return isnan(ctx->gps.lat) ? 0 : 1;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/lat)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon is_connected info
 */
float get_gnss_lat(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return NAN;
    }
    return ctx->gps.lat;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/lon)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon is_connected info
 */
float get_gnss_lon(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return NAN;
    }
    return ctx->gps.lon;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/hpe)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon hpe info
 */
int64_t get_gnss_hpe(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->gps.hpe;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/alt)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon is_connected info
 */
float get_gnss_alt(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return NAN;
    }
    return ctx->gps.alt;
}
/*! \brief field extraction for dynamic use of Nanopb (gnss/vpe)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon vpe info
 */
int64_t get_gnss_vpe(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->gps.vpe;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/speed)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon is_connected info
 */
float get_gnss_speed(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return NAN;
    }
    return ctx->gps.speed;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/bearing)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon bearing info
 */
int64_t get_gnss_bearing(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->gps.bearing;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/nsat)
 *
 *  @param ctx workspace buffer
 *  @param idx index (unused)
 *
 *  @return beacon nsat info
 */
int64_t get_gnss_nsat(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL || isnan(ctx->gps.lat)) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->gps.nsat;
}

/*! \brief field extraction for dynamic use of Nanopb (gnss/timestamp)
 *
 *  @param ctx workspace buffer
 *  @param idx index into beacons
 *
 *  @return timestamp info
 */
int64_t get_gnss_age(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }
    return ctx->gps.age;
}

/*! \brief Get a cell
 *
 *  @param ctx workspace buffer
 *  @param idx index into cells
 *
 *  @return Pointer to cell
 */
Beacon_t *get_cell(Sky_ctx_t *ctx, uint32_t idx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param")
        return 0;
    }

    return &ctx->beacon[ctx->ap_len + idx];
}

/*! \brief Get cell type
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell type
 */
int16_t get_cell_type(Beacon_t *cell)
{
    return cell->h.type;
}

/*! \brief Get cell id1
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell id1, -1 if not available
 */
int64_t get_cell_id1(Beacon_t *cell)
{
    uint16_t type = get_cell_type(cell);

    switch (type) {
    case SKY_BEACON_CDMA:
        return SKY_UNKNOWN_ID1; // ID1 irrelevant for CDMA.
    case SKY_BEACON_GSM:
    case SKY_BEACON_LTE:
    case SKY_BEACON_NBIOT:
    case SKY_BEACON_UMTS:
    case SKY_BEACON_NR:
        return cell->cell.id1;
    }

    return 0;
}

/*! \brief Get cell id2
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell id2, -1 if not available
 */
int64_t get_cell_id2(Beacon_t *cell)
{
    return cell->cell.id2;
}

/*! \brief Get cell id3
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell id3, -1 if not available
 */
int64_t get_cell_id3(Beacon_t *cell)
{
    return cell->cell.id3;
}

/*! \brief Get cell id4
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell id4, -1 if not available
 */
int64_t get_cell_id4(Beacon_t *cell)
{
    return cell->cell.id4;
}

/*! \brief Get cell id5
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell id5, -1 if not available
 */
int64_t get_cell_id5(Beacon_t *cell)
{
    uint16_t type = get_cell_type(cell);

    switch (type) {
    case SKY_BEACON_CDMA:
        return SKY_UNKNOWN_ID5; // Reporting ID5 value not supported for CDMA
    case SKY_BEACON_GSM:
        return SKY_UNKNOWN_ID5; // Reporting ID5 value not supported for GSM
    case SKY_BEACON_LTE:
    case SKY_BEACON_NBIOT:
    case SKY_BEACON_UMTS:
    case SKY_BEACON_NR:
        return cell->cell.id5;
    }

    return 0;
}

/*! \brief Get cell id6
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell id6, -1 if not available
 */
int64_t get_cell_id6(Beacon_t *cell)
{
    uint16_t type = get_cell_type(cell);

    switch (type) {
    case SKY_BEACON_CDMA:
        return SKY_UNKNOWN_ID6; // Reporting ID6 value not supported for CDMA
    case SKY_BEACON_GSM:
        return SKY_UNKNOWN_ID6; // Reporting ID6 value not supported for GSM
    case SKY_BEACON_LTE:
    case SKY_BEACON_NBIOT:
    case SKY_BEACON_UMTS:
    case SKY_BEACON_NR:
        return cell->cell.freq;
    }

    return 0;
}

/*! \brief Get cell connected flag
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return cell connected flag
 */
bool get_cell_connected_flag(Sky_ctx_t *ctx, Beacon_t *cell)
{
    return ctx->connected >= 0 && &ctx->beacon[ctx->connected] == cell;
}

/*! \brief Return cell RSSI value
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return beacon rssi
 */
int64_t get_cell_rssi(Beacon_t *cell)
{
    return cell->h.rssi;
}

/*! \brief Return cell age value
 *
 *  @param cell Pointer to beacon (cell)
 *
 *  @return beacon age
 */
int64_t get_cell_age(Beacon_t *cell)
{
    return cell->h.age;
}

/*! \brief generate random byte sequence
 *
 *  @param rand_buf pointer to buffer where rand bytes are put
 *  @param bufsize length of rand bytes
 *
 *  @returns 0 for failure, length of rand sequence for success
 */
int sky_rand_fn(uint8_t *rand_buf, uint32_t bufsize)
{
    int i;

    if (!rand_buf)
        return 0;

    for (i = 0; i < bufsize; i++)
        rand_buf[i] = rand() % 256;
    return bufsize;
}
