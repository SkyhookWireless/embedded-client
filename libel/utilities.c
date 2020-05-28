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

/*! \brief validate the cache buffer - Cant use LOGFMT here
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
            (*logf)(SKY_LOG_LEVEL_WARNING, "Cache validation failed: NUL pointer");
#endif
        return false;
    }

    if (c->len != CACHE_SIZE) {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_ERROR, "Cache validation failed: too big for CACHE_SIZE");
#endif
        return false;
    }
    if (c->newest >= CACHE_SIZE) {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_ERROR, "Cache validation failed: newest too big for CACHE_SIZE");
#endif
        return false;
    }

    if (c->header.magic != SKY_MAGIC) {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_ERROR, "Cache validation failed: bad magic in header");
#endif
        return false;
    }
    if (c->header.crc32 ==
        sky_crc32(&c->header.magic, (uint8_t *)&c->header.crc32 - (uint8_t *)&c->header.magic)) {
        for (i = 0; i < CACHE_SIZE; i++) {
            if (c->cacheline[i].len > TOTAL_BEACONS) {
#if SKY_DEBUG
                if (logf != NULL)
                    (*logf)(SKY_LOG_LEVEL_ERROR,
                        "Cache validation failed: too many beacons for TOTAL_BEACONS");
#endif
                return false;
            }

            for (j = 0; j < TOTAL_BEACONS; j++) {
                if (c->cacheline[i].beacon[j].h.magic != BEACON_MAGIC) {
#if SKY_DEBUG
                    if (logf != NULL)
                        (*logf)(SKY_LOG_LEVEL_ERROR, "Cache validation failed: Bad beacon info");
#endif
                    return false;
                }
                if (c->cacheline[i].beacon[j].h.type > SKY_BEACON_MAX) {
#if SKY_DEBUG
                    if (logf != NULL)
                        (*logf)(SKY_LOG_LEVEL_ERROR, "Cache validation failed: Bad beacon type");
#endif
                    return false;
                }
            }
        }
    } else {
#if SKY_DEBUG
        if (logf != NULL)
            (*logf)(SKY_LOG_LEVEL_ERROR, "Cache validation failed: crc mismatch!");
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
 *  @param file the file name where LOG_BUFFER was invoked
 *  @param function the function name where LOG_BUFFER was invoked
 *  @param ctx workspace pointer
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

/*! \brief dump Virtual APs in group (children not parent)
 *
 *  @param ctx workspace pointer
 *  @param b parent of Virtual Group AP
 *  @param file the file name where LOG_BUFFER was invoked
 *  @param function the function name where LOG_BUFFER was invoked
 *
 *  @returns void
 */
void dump_vap(Sky_ctx_t *ctx, Beacon_t *b, const char *file, const char *func)
{
#if SKY_DEBUG
    int j, n, value;
    Vap_t *vap = b->ap.vg;
    uint8_t mac[MAC_SIZE];
    int idx_b;
    char *prefix;

    /* Test whether beacon is in cache or workspace */
    if (b >= ctx->beacon && b < ctx->beacon + MAX_AP_BEACONS + 1) {
        idx_b = b - ctx->beacon;
        prefix = "worksp VAP:";
    } else if (b >= ctx->cache->cacheline[0].beacon &&
               b < ctx->cache->cacheline[CACHE_SIZE].beacon) {
        idx_b = b - ctx->cache->cacheline[0].beacon;
        idx_b %= MAX_AP_BEACONS;
        prefix = " cache VAP:";
    } else {
        idx_b = 0;
        prefix = "unknown VAP:";
    }
    if (idx_b > MAX_AP_BEACONS || idx_b < 0) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Out of bounds!");
        return;
    }
    if (!b->ap.vg_len)
        return;

    for (j = 0; j < b->ap.vg_len; j++) {
        memcpy(mac, b->ap.mac, MAC_SIZE);
        n = vap[j + 2].data.nibble_idx;
        value = vap[j + 2].data.value;
        if (n & 1)
            mac[n / 2] = ((mac[n / 2] & 0xF0) | value);
        else
            mac[n / 2] = ((mac[n / 2] & 0x0F) | (value << 4));

        logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
            "%s %-2d: WiFi Age: %d %s MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %-4d %-4d MHz VAP(%d 0x%01X)",
            prefix, idx_b, b->ap.age, " ^^^^ ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
            b->ap.rssi, b->ap.freq, n, value);
    }
#endif
}

/*! \brief dump AP including any VAP
 *
 *  @param ctx workspace pointer
 *  @param str comment
 *  @param b pointer to Beacon_t structure
 *  @param file the file name where LOG_BUFFER was invoked
 *  @param function the function name where LOG_BUFFER was invoked
 *
 *  @returns void
 */
void dump_ap(Sky_ctx_t *ctx, char *str, Beacon_t *b, const char *file, const char *func)
{
#if SKY_DEBUG
    int idx_b, cached = 0;

    if (str == NULL)
        str = "AP:";
    /* Test whether beacon is in cache or workspace */
    if (b >= ctx->beacon && b < ctx->beacon + MAX_AP_BEACONS + 1)
        idx_b = b - ctx->beacon;
    else if (b >= ctx->cache->cacheline[0].beacon &&
             b < ctx->cache->cacheline[CACHE_SIZE].beacon +
                     ctx->cache->cacheline[CACHE_SIZE].ap_len) {
        cached = 1;
        idx_b = b - ctx->cache->cacheline[0].beacon;
        idx_b %= MAX_AP_BEACONS;
    } else
        idx_b = 0;
    if (idx_b > MAX_AP_BEACONS || idx_b < 0) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Out of bounds!");
        return;
    }
    logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
        "%s %-2d: WiFi Age: %d %s MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %-4d %-4d MHz vap: %d",
        str, idx_b, b->ap.age,
        (cached || b->ap.property.in_cache) ? (b->ap.property.used ? "Used  " : "Unused") :
                                              "      ",
        b->ap.mac[0], b->ap.mac[1], b->ap.mac[2], b->ap.mac[3], b->ap.mac[4], b->ap.mac[5],
        b->ap.rssi, b->ap.freq, b->ap.vg_len);
    dump_vap(ctx, b, file, func);
#endif
}

/*! \brief dump the beacons in the workspace including any VAP
 *
 *  @param ctx workspace pointer
 *  @param file the file name where LOG_BUFFER was invoked
 *  @param function the function name where LOG_BUFFER was invoked
 *
 *  @returns 0 for success or negative number for error
 */
void dump_workspace(Sky_ctx_t *ctx, const char *file, const char *func)
{
#if SKY_DEBUG
    int i;

    logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
        "dump_workspace: Got %d beacons, WiFi %d, connected %d", ctx->len, ctx->ap_len,
        ctx->connected);
    for (i = 0; i < ctx->len; i++) {
        switch (ctx->beacon[i].h.type) {
        case SKY_BEACON_AP:
            dump_ap(ctx, " workspace:", &ctx->beacon[i], file, func);
            break;
        case SKY_BEACON_CDMA:
            logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                " workspace: %-2d: CDMA Age: %d sid: %d, nid: %d, bsid: %d, rssi: %d", i,
                ctx->beacon[i].cdma.age, ctx->beacon[i].cdma.sid, ctx->beacon[i].cdma.nid,
                ctx->beacon[i].cdma.bsid, ctx->beacon[i].cdma.rssi);
            break;
        case SKY_BEACON_GSM:
            logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                " workspace: %-2d: GSM Age: %d lac: %d, ui: %d, mcc: %d, mnc: %d, rssi: %d", i,
                ctx->beacon[i].gsm.age, ctx->beacon[i].gsm.lac, ctx->beacon[i].gsm.ci,
                ctx->beacon[i].gsm.mcc, ctx->beacon[i].gsm.mnc, ctx->beacon[i].gsm.rssi);
            break;
        case SKY_BEACON_LTE:
            logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                " workspace: %-2d: LTE Age: %d e-cellid: %d, mcc: %d, mnc: %d, tac: %d, rssi: %d",
                i, ctx->beacon[i].lte.age, ctx->beacon[i].lte.e_cellid, ctx->beacon[i].lte.mcc,
                ctx->beacon[i].lte.mnc, ctx->beacon[i].lte.tac, ctx->beacon[i].lte.rssi);
            break;
        case SKY_BEACON_NBIOT:
            logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                " workspace: %-2d: NB-IoT Age: %d mcc: %d, mnc: %d, e_cellid: %d, tac: %d, rssi: %d",
                i, ctx->beacon[i].nbiot.age, ctx->beacon[i].nbiot.mcc, ctx->beacon[i].nbiot.mnc,
                ctx->beacon[i].nbiot.e_cellid, ctx->beacon[i].nbiot.tac, ctx->beacon[i].nbiot.rssi);
            break;
        case SKY_BEACON_UMTS:
            logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                " workspace: %-2d: UMTS Age: %d lac: %d, ucid: %d, mcc: %d, mnc: %d, rssi: %d", i,
                ctx->beacon[i].umts.age, ctx->beacon[i].umts.lac, ctx->beacon[i].umts.ucid,
                ctx->beacon[i].umts.mcc, ctx->beacon[i].umts.mnc, ctx->beacon[i].umts.rssi);
            break;
        case SKY_BEACON_BLE:
        case SKY_BEACON_MAX:
        default:
            logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG, "Beacon %-2d: Type: Unknown", i);
            break;
        }
    }
    if (CONFIG(ctx->cache, last_config_time) == 0) {
        logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
            "Config: Beacons:%d AP:%d VAP:%d(%d) Thresholds:%d & %d(Match) %d(Age) %d(Beacon) %d(RSSI) Update:Pending",
            CONFIG(ctx->cache, total_beacons), CONFIG(ctx->cache, max_ap_beacons),
            CONFIG(ctx->cache, max_vap_per_ap), CONFIG(ctx->cache, max_vap_per_rq),
            CONFIG(ctx->cache, cache_match_used_threshold),
            CONFIG(ctx->cache, cache_match_all_threshold), CONFIG(ctx->cache, cache_age_threshold),
            CONFIG(ctx->cache, cache_beacon_threshold),
            CONFIG(ctx->cache, cache_neg_rssi_threshold),
            ctx->header.time - CONFIG(ctx->cache, last_config_time));
    } else {
        logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
            "Config: Beacons:%d AP:%d VAP:%d(%d) Thresholds:%d & %d(Match) %d(Age) %d(Beacon) %d(RSSI) Update:%d Sec",
            CONFIG(ctx->cache, total_beacons), CONFIG(ctx->cache, max_ap_beacons),
            CONFIG(ctx->cache, max_vap_per_ap), CONFIG(ctx->cache, max_vap_per_rq),
            CONFIG(ctx->cache, cache_match_used_threshold),
            CONFIG(ctx->cache, cache_match_all_threshold), CONFIG(ctx->cache, cache_age_threshold),
            CONFIG(ctx->cache, cache_beacon_threshold),
            CONFIG(ctx->cache, cache_neg_rssi_threshold),
            (int)((*ctx->gettime)(NULL)-CONFIG(ctx->cache, last_config_time)));
    }
#endif
}

/*! \brief dump the beacons in the cache
 *
 *  @param ctx workspace pointer
 *  @param file the file name where LOG_BUFFER was invoked
 *  @param function the function name where LOG_BUFFER was invoked
 *
 *  @returns 0 for success or negative number for error
 */
void dump_cache(Sky_ctx_t *ctx, const char *file, const char *func)
{
#if SKY_DEBUG
    int i, j;
    Sky_cacheline_t *c;
    Beacon_t *b;

    for (i = 0; i < CACHE_SIZE; i++) {
        c = &ctx->cache->cacheline[i];
        if (c->len == 0 || c->time == 0) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d of %d - empty len:%d ap_len:%d time:%u", i,
                ctx->cache->len, c->len, c->ap_len, c->time);
        } else {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d of %d%s GPS:%d.%06d,%d.%06d,%d", i,
                ctx->cache->len, ctx->cache->newest == i ? "<-newest" : "", (int)c->loc.lat,
                (int)fabs(round(1000000 * (c->loc.lat - (int)c->loc.lat))), (int)c->loc.lon,
                (int)fabs(round(1000000 * (c->loc.lon - (int)c->loc.lon))), c->loc.hpe);
            for (j = 0; j < c->len; j++) {
                b = &c->beacon[j];
                switch (b->h.type) {
                case SKY_BEACON_AP:
                    dump_ap(ctx, "     cache:", b, file, func);
                    break;
                case SKY_BEACON_CDMA:
                    logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                        "     cache: %-2d:%-2d: Type: CDMA, sid: %d, nid: %d, bsid: %d, rssi: %d",
                        i, j, b->cdma.sid, b->cdma.nid, b->cdma.bsid, b->cdma.rssi);
                    break;
                case SKY_BEACON_GSM:
                    logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                        "     cache: %-2d:%-2d: Type: GSM, lac: %d, ui: %d, mcc: %d, mnc: %d, rssi: %d",
                        i, j, b->gsm.lac, b->gsm.ci, b->gsm.mcc, b->gsm.mnc, b->gsm.rssi);
                    break;
                case SKY_BEACON_LTE:
                    logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                        "     cache: %-2d:%-2d: Age: %d Type: LTE, e-cellid: %d, mcc: %d, mnc: %d, tac: %d, rssi: %d",
                        i, j, b->lte.age, b->lte.e_cellid, b->lte.mcc, b->lte.mnc, b->lte.tac,
                        b->lte.rssi);
                    break;
                case SKY_BEACON_NBIOT:
                    logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                        "     cache: %-2d:%-2d: Type: NB-IoT, mcc: %d, mnc: %d, e_cellid: %d, tac: %d, rssi: %d",
                        i, j, b->nbiot.mcc, b->nbiot.mnc, b->nbiot.e_cellid, b->nbiot.tac,
                        b->nbiot.rssi);
                    break;
                case SKY_BEACON_UMTS:
                    logfmt(file, func, ctx, SKY_LOG_LEVEL_DEBUG,
                        "     cache: %-2d:%-2d: Type: UMTS, lac: %d, ucid: %d, mcc: %d, mnc: %d, rssi: %d",
                        i, j, b->umts.lac, b->umts.ucid, b->umts.mcc, b->umts.mnc, b->umts.rssi);
                    break;
                case SKY_BEACON_BLE:
                case SKY_BEACON_MAX:
                    LOGFMT(
                        ctx, SKY_LOG_LEVEL_DEBUG, "     cache: %-2d:%-2d: Type: Unknown!!!", i, j);
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
    if (CONFIG(c, cache_match_used_threshold) == 0)
        CONFIG(c, cache_match_used_threshold) = CACHE_MATCH_THRESHOLD_USED;
    if (CONFIG(c, cache_match_all_threshold) == 0)
        CONFIG(c, cache_match_all_threshold) = CACHE_MATCH_THRESHOLD_ALL;
    if (CONFIG(c, cache_age_threshold) == 0)
        CONFIG(c, cache_age_threshold) = CACHE_AGE_THRESHOLD;
    if (CONFIG(c, cache_beacon_threshold) == 0)
        CONFIG(c, cache_beacon_threshold) = CACHE_BEACON_THRESHOLD;
    if (CONFIG(c, cache_neg_rssi_threshold) == 0)
        CONFIG(c, cache_neg_rssi_threshold) = CACHE_RSSI_THRESHOLD;
    if (CONFIG(c, max_vap_per_ap) == 0)
        CONFIG(c, max_vap_per_ap) = MAX_VAP_PER_AP;
    if (CONFIG(c, max_vap_per_rq) == 0)
        CONFIG(c, max_vap_per_rq) = MAX_VAP_PER_RQ;
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
        return 0;
    }
    return ctx->beacon[idx].ap.rssi;
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
        return 0;
    }
    return ctx->beacon[idx].ap.age;
}

/*! \brief field extraction for dynamic use of Nanopb (num gsm)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of gsm beacons
 */
int32_t get_num_gsm(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
        return 0;
    }
    return ctx->beacon[get_base_beacons(ctx, SKY_BEACON_UMTS) + idx].umts.age;
}

/*! \brief field extraction for dynamic use of Nanopb (num gnss)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of gnss
 */
int32_t get_num_gnss(Sky_ctx_t *ctx)
{
    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
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
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad param");
        return 0;
    }
    return ctx->gps.age;
}

/*! \brief field extraction for dynamic use of Nanopb (num vaps)
 *
 *  @param ctx workspace buffer
 *
 *  @return number of bytes of compressed Virtual AP groups
 */
int32_t get_num_vaps(Sky_ctx_t *ctx)
{
    int j, nv = 0;
#if SKY_DEBUG
    int total_vap = 0;
#endif
    Beacon_t *w;

    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
        return 0;
    }
    for (j = 0; j < NUM_APS(ctx); j++) {
        w = &ctx->beacon[j];
        nv += (w->ap.vg_len ? 1 : 0);
#if SKY_DEBUG
        total_vap += w->ap.vg[VAP_LENGTH].len;
#endif
    }

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Groups: %d, vaps: %d", nv, total_vap);
    return nv;
}

/*! \brief field extraction for dynamic use of Nanopb (vap_data)
 *
 *  this routine also patches the index of the parent AP
 *
 *  @param ctx workspace buffer
 *  @param idx index into Virtual Groups
 *
 *  @return vaps data i.e len, AP, patch1, patch2...
 */
uint8_t *get_vap_data(Sky_ctx_t *ctx, uint32_t idx)
{
    int j, nvg = 0;
    Beacon_t *w;

    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
        return 0;
    }
    /* Walk through APs counting vap, when the idx is the current Virtual Group */
    /* return the Virtual AP data */
    for (j = 0; j < NUM_APS(ctx); j++) {
        w = &ctx->beacon[j];
        if ((w->ap.vg_len ? 1 : 0) && nvg == idx) {
#if 0
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "AP: %d idx: %d len: %d ap: %d", j, idx,
                w->ap.vg[VAP_LENGTH].len, w->ap.vg[VAP_PARENT].ap);
            dump_hex16(__FILE__, __FUNCTION__, ctx, SKY_LOG_LEVEL_DEBUG, w->ap.vg + 1,
                w->ap.vg[VAP_LENGTH].len, 0);
#endif
            return (uint8_t *)w->ap.vg;
        } else {
            nvg += (w->ap.vg_len ? 1 : 0);
        }
    }
    return 0;
}

/*! \brief trim VAP children to meet max_vap_per_rq config
 *
 *  this routine alters the vap patch data, reducing where necessary
 *  the number of children in a virtual group so that as many groups
 *  as possible are retained, but the max_vap_per_rq is not exceeded
 *
 *  @param ctx workspace buffer
 *
 *  @return vaps data i.e len, AP, patch1, patch2...
 */
uint8_t *select_vap(Sky_ctx_t *ctx)
{
    int j, nvap = 0, no_more = false;
    Beacon_t *w;
    uint8_t cap_vap[MAX_AP_BEACONS] = {
        0
    }; /* fill request with as many virtual groups as possible */

    if (ctx == NULL) {
        // LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad param");
        return 0;
    }
    for (; !no_more && nvap < CONFIG(ctx->cache, max_vap_per_rq);) {
        /* Walk through APs counting vap, when the idx is the current Virtual Group */
        /* return the Virtual AP data */
        no_more = true;
        for (j = 0; j < NUM_APS(ctx); j++) {
            w = &ctx->beacon[j];
            if (w->ap.vg_len > cap_vap[j]) {
                cap_vap[j]++;
                nvap++;
                if (nvap == CONFIG(ctx->cache, max_vap_per_rq))
                    break;
                if (w->ap.vg_len > cap_vap[j])
                    no_more = false;
            }
        }
    }
    /* Complete the virtual group patch bytes with index of parent and update length */
    for (j = 0; j < NUM_APS(ctx); j++) {
        w = &ctx->beacon[j];
        w->ap.vg[VAP_PARENT].ap = j;
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "AP: %d len: %d -> %d", w->ap.vg[VAP_PARENT].ap,
            w->ap.vg[VAP_LENGTH].len, cap_vap[j] + VAP_PARENT);
        w->ap.vg[VAP_LENGTH].len = cap_vap[j] ? cap_vap[j] + VAP_PARENT : 0;
        dump_hex16(__FILE__, __FUNCTION__, ctx, SKY_LOG_LEVEL_DEBUG, w->ap.vg + 1,
            w->ap.vg[VAP_LENGTH].len, 0);
    }
    return 0;
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
