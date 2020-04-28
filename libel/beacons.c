/*! \file libel/beacons.c
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
#include "../.submodules/tiny-AES128-C/aes.h"
#define SKY_LIBEL 1
#include "libel.h"

/* #define VERBOSE_DEBUG 1 */

#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define NOMINAL_RSSI(b) ((b) == -1 ? (-90) : (b))
#define PUT_IN_CACHE true
#define GET_FROM_CACHE false

void dump_workspace(Sky_ctx_t *ctx);
void dump_cache(Sky_ctx_t *ctx);
static bool beacon_in_cache(Sky_ctx_t *ctx, Beacon_t *b, int *idx_cl, int *idx_b);
static bool beacon_in_cacheline(Sky_ctx_t *ctx, Beacon_t *b, Sky_cacheline_t *cl, int *index);

/*! \brief test two MAC addresses for being virtual aps
 *
 *  @param macA pointer to the first MAC
 *  @param macB pointer to the second MAC
 *
 *  @return -1, 0 or 1
 *  return 0 when NOT similar, -1 indicates keep A, 1 keep B
 */
static int similar(uint8_t macA[], uint8_t macB[])
{
    /* Return 1 (true) if OUIs are identical and no more than 1 hex digits
     * differ between the two MACs. Else return 0 (false).
     */
    size_t num_diff = 0; // Num hex digits which differ
    size_t i;

    if (memcmp(macA, macB, 3) != 0)
        return 0;

    for (i = 3; i < MAC_SIZE; i++) {
        if (((macA[i] & 0xF0) != (macB[i] & 0xF0) && ++num_diff > 1) ||
            ((macA[i] & 0x0F) != (macB[i] & 0x0F) && ++num_diff > 1))
            return 0;
    }

    /* MACs are similar, choose one to remove */
    return (memcmp(macA + 3, macB + 3, MAC_SIZE - 3) < 0 ? -1 : 1);
}

/*! \brief shuffle list to remove the beacon at index
 *
 *  @param ctx Skyhook request context
 *  @param index 0 based index of AP to remove
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static Sky_status_t remove_beacon(Sky_ctx_t *ctx, int index)
{
    if (index >= ctx->len)
        return SKY_ERROR;

    if (ctx->beacon[index].h.type == SKY_BEACON_AP)
        ctx->ap_len -= 1;

    memmove(
        &ctx->beacon[index], &ctx->beacon[index + 1], sizeof(Beacon_t) * (ctx->len - index - 1));
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "idx:%d", index)
    ctx->len -= 1;
    return SKY_SUCCESS;
}

/*! \brief insert beacon in list based on type and AP rssi
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno pointer to errno
 *  @param b beacon to add
 *  @param index pointer where to save the insert position
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static Sky_status_t insert_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, Beacon_t *b, int *index)
{
    int i;

    /* sanity checks */
    if (!validate_workspace(ctx) || b->h.magic != BEACON_MAGIC || b->h.type >= SKY_BEACON_MAX) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Invalid params. Beacon type %s", sky_pbeacon(b))
        return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
    }

    /* find correct position to insert based on type */
    for (i = 0; i < ctx->len; i++)
        if (ctx->beacon[i].h.type >= b->h.type)
            break;

    /* add beacon at the end */
    if (i == ctx->len) {
        ctx->beacon[i] = *b;
        ctx->len++;
    } else {
        /* if AP, add in rssi order */
        if (b->h.type == SKY_BEACON_AP) {
            for (; i < ctx->ap_len; i++)
                if (ctx->beacon[i].h.type != SKY_BEACON_AP ||
                    NOMINAL_RSSI(ctx->beacon[i].ap.rssi) > NOMINAL_RSSI(b->ap.rssi))
                    break;
        }
        /* shift beacons to make room for the new one */
        memmove(&ctx->beacon[i + 1], &ctx->beacon[i], sizeof(Beacon_t) * (ctx->len - i));
        ctx->beacon[i] = *b;
        ctx->len++;
    }
    /* report back the position beacon was added */
    if (index != NULL)
        *index = i;
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon type %s inserted idx: %d", sky_pbeacon(b), i)

    if (b->h.type == SKY_BEACON_AP)
        ctx->ap_len++;
    return SKY_SUCCESS;
}

/*! \brief try to reduce AP by filtering out based on diversity of rssi
 *
 *  @param ctx Skyhook request context
 *
 *  @return sky_status_t SKY_SUCCESS if beacon removed or SKY_ERROR
 */
static Sky_status_t filter_by_rssi(Sky_ctx_t *ctx)
{
    int i, reject, jump, up_down;
    float band_range, worst;
    float ideal_rssi[MAX_AP_BEACONS + 1];

    if (ctx->ap_len <= CONFIG(ctx->cache, max_ap_beacons))
        return SKY_ERROR;

    /* what share of the range of rssi values does each beacon represent */
    band_range = (NOMINAL_RSSI(ctx->beacon[ctx->ap_len - 1].ap.rssi) -
                     NOMINAL_RSSI(ctx->beacon[0].ap.rssi)) /
                 ((float)ctx->ap_len - 1);

    /* if the rssi range is small, throw away middle beacon */

    if (band_range < 0.5) {
        /* search from middle of range looking for beacon not in cache */
        for (jump = 0, up_down = -1, i = ctx->ap_len / 2; i >= 0 && i < ctx->ap_len;
             jump++, i += up_down * jump, up_down = -up_down) {
            if (!ctx->beacon[i].ap.property.in_cache) {
                LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small. %s beacon",
                    !jump ? "Remove middle" : "Found non-cached")
                return remove_beacon(ctx, i);
            }
        }
        /* search from middle of range looking for Unused beacon in cache */
        for (jump = 0, up_down = -1, i = ctx->ap_len / 2; i >= 0 && i < ctx->ap_len;
             jump++, i += up_down * jump, up_down = -up_down) {
            if (ctx->beacon[i].ap.property.in_cache && !ctx->beacon[i].ap.property.used) {
                LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small. %s beacon",
                    !jump ? "Remove middle Unused" : "Found Unused")
                return remove_beacon(ctx, i);
            }
        }
        LOGFMT(
            ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small. Removing cached Used beacon")
        return remove_beacon(ctx, ctx->ap_len / 2);
    }

    /* if beacon with min RSSI is below threshold, throw out weak one, not in cache or Unused */
    if (NOMINAL_RSSI(ctx->beacon[0].ap.rssi) < -CONFIG(ctx->cache, cache_neg_rssi_threshold)) {
        for (i = 0, reject = -1; i < ctx->ap_len && reject == -1; i++) {
            if (ctx->beacon[i].ap.rssi < -CONFIG(ctx->cache, cache_neg_rssi_threshold) &&
                !ctx->beacon[i].ap.property.in_cache)
                reject = i;
        }
        for (i = 0; i < ctx->ap_len && reject == -1; i++) {
            if (ctx->beacon[i].ap.rssi < -CONFIG(ctx->cache, cache_neg_rssi_threshold) &&
                ctx->beacon[i].ap.property.in_cache && !ctx->beacon[i].ap.property.used)
                reject = i;
        }
        if (reject == -1)
            reject = 0; // reject lowest rssi value if there is no uncached or Unused beacon
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Discarding beacon %d with very weak strength", 0)
        return remove_beacon(ctx, reject);
    }

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "range: %d band range: %d.%02d",
        (NOMINAL_RSSI(ctx->beacon[ctx->ap_len - 1].ap.rssi) - NOMINAL_RSSI(ctx->beacon[0].ap.rssi)),
        (int)band_range, (int)fabs(round(100 * (band_range - (int)band_range))))

    /* for each beacon, work out it's ideal rssi value to give an even distribution */
    for (i = 0; i < ctx->ap_len; i++)
        ideal_rssi[i] = NOMINAL_RSSI(ctx->beacon[0].ap.rssi) + (i * band_range);

    /* find AP with poorest fit to ideal rssi */
    /* always keep lowest and highest rssi */
    for (i = 1, reject = -1, worst = 0; i < ctx->ap_len - 1; i++) {
        if (!ctx->beacon[i].ap.property.in_cache &&
            fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]) > worst) {
            worst = fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]);
            reject = i;
        }
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons */
        /* Throw away either lowest or highest rssi valued beacons if not in cache */
        if (!ctx->beacon[0].ap.property.in_cache)
            reject = 0;
        else if (!ctx->beacon[ctx->ap_len - 1].ap.property.in_cache)
            reject = ctx->ap_len - 1;
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons */
        /* Throw away Unused beacon with worst fit */
        for (i = 1, worst = 0; i < ctx->ap_len - 1; i++) {
            if (!ctx->beacon[i].ap.property.used &&
                fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]) > worst) {
                worst = fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]);
                reject = i;
            }
        }
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons and no Unused */
        /* Throw away either lowest or highest rssi valued beacons if not Used */
        if (!ctx->beacon[0].ap.property.used)
            reject = 0;
        else if (!ctx->beacon[ctx->ap_len - 1].ap.property.used)
            reject = ctx->ap_len - 1;
        else
            reject = ctx->ap_len / 2; /* remove middle beacon (all beacons are in cache and Used) */
    }
#if SKY_DEBUG
    for (i = 0; i < ctx->ap_len; i++) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%s: %-2d %s ideal %d.%02d fit %2d.%02d (%d)",
            (reject == i) ? "remove" : "      ", i,
            ctx->beacon[i].ap.property.in_cache ?
                ctx->beacon[i].ap.property.used ? "Used  " : "Unused" :
                "      ",
            (int)ideal_rssi[i], (int)fabs(round(100 * (ideal_rssi[i] - (int)ideal_rssi[i]))),
            (int)fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]), ideal_rssi[i],
            (int)fabs(
                round(100 * (fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]) -
                                (int)fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i])))),
            ctx->beacon[i].ap.rssi)
    }
#endif
    return remove_beacon(ctx, reject);
}

/*! \brief try to reduce AP by filtering out virtual AP
 *         When similar, remove beacon with highesr mac address
 *         unless it is in cache, then choose to remove the uncached beacon
 *
 *  @param ctx Skyhook request context
 *
 *  @return true if beacon removed or false otherwise
 */
static bool filter_virtual_aps(Sky_ctx_t *ctx)
{
    int i, j;
    int cmp = 0, rm = -1;
#if SKY_DEBUG
    int keep = -1;
    bool cached = false;
    Beacon_t *b;
#endif

    LOGFMT(
        ctx, SKY_LOG_LEVEL_DEBUG, "ap_len: %d APs of %d beacons", (int)ctx->ap_len, (int)ctx->len)

    dump_workspace(ctx);

    if (ctx->ap_len <= CONFIG(ctx->cache, max_ap_beacons)) {
        return false;
    }

    /* look for any AP beacon that is 'similar' to another */
    if (ctx->beacon[0].h.type != SKY_BEACON_AP) {
        LOGFMT(ctx, SKY_LOG_LEVEL_CRITICAL, "beacon type not WiFi")
        return false;
    }

    for (j = 0; j < ctx->ap_len - 1; j++) {
        b = &ctx->beacon[j];
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cmp MAC %02X:%02X:%02X:%02X:%02X:%02X %d", b->ap.mac[0],
            b->ap.mac[1], b->ap.mac[2], b->ap.mac[3], b->ap.mac[4], b->ap.mac[5], cmp)
        for (i = j + 1; i < ctx->ap_len; i++) {
            b = &ctx->beacon[i];
            if ((cmp = similar(ctx->beacon[i].ap.mac, ctx->beacon[j].ap.mac)) < 0) {
                if (ctx->beacon[j].ap.property.in_cache) {
                    rm = i;
#if SKY_DEBUG
                    keep = j;
                    cached = true;
#endif
                } else {
                    rm = j;
#if SKY_DEBUG
                    keep = i;
#endif
                }
            } else if (cmp > 0) {
                if (ctx->beacon[i].ap.property.in_cache) {
                    rm = j;
#if SKY_DEBUG
                    keep = i;
                    cached = true;
#endif
                } else {
                    rm = i;
#if SKY_DEBUG
                    keep = j;
#endif
                }
            }
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "    MAC %02X:%02X:%02X:%02X:%02X:%02X %d",
                b->ap.mac[0], b->ap.mac[1], b->ap.mac[2], b->ap.mac[3], b->ap.mac[4], b->ap.mac[5],
                cmp)
            if (rm != -1) {
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d similar to %d%s", rm, keep,
                    cached ? " (cached)" : "")
                remove_beacon(ctx, rm);
                return true;
            }
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "no match")
    return false;
}

/*! \brief add beacon to list in workspace context
 *
 *   if beacon is not AP and workspace is full (of non-AP), error
 *   if beacon is AP,
 *    . reject a duplicate
 *    . for duplicates, keep newest and strongest
 *     
 *   Insert new beacon in workspace
 *    . Add APs in order based on lowest to highest rssi value
 *    . Add cells after APs
 *
 *   If AP just added is known in cache,
 *    . set cached and copy Used property from cache
 *
 *   If AP just added fills workspace, remove one AP,
 *    . Remove one virtual AP if there is a match
 *    . If haven't removed one AP, remove one based on rssi distribution
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param b pointer to new beacon
 *  @param is_connected This beacon is currently connected
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
Sky_status_t add_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, Beacon_t *b, bool is_connected)
{
    int i = -1, j = 0, idx_cl = 0;
    int dup = -1;
    Beacon_t *wb, *cb;

    /* don't add any more non-AP beacons if we've already hit the limit of non-AP beacons */
    if (b->h.type != SKY_BEACON_AP &&
        ctx->len - ctx->ap_len >
            (CONFIG(ctx->cache, total_beacons) - CONFIG(ctx->cache, max_ap_beacons))) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "too many (b->h.type: %s) (ctx->len - ctx->ap_len: %d)",
            sky_pbeacon(b), ctx->len - ctx->ap_len)
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);
    } else if (b->h.type == SKY_BEACON_AP) {
        if (!validate_mac(b->ap.mac, ctx))
            return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
        /* see if this mac already added (duplicate beacon) */
        for (dup = 0; dup < ctx->ap_len; dup++) {
            if (memcmp(b->ap.mac, ctx->beacon[dup].ap.mac, MAC_SIZE) == 0) {
                break;
            }
        }
        /* if it is already in workspace */
        if (dup < ctx->ap_len) {
            /* reject new beacon if older or weaker */
            if (b->ap.age > ctx->beacon[dup].ap.age ||
                (b->ap.age == ctx->beacon[dup].ap.age &&
                    NOMINAL_RSSI(b->ap.rssi) <= NOMINAL_RSSI(ctx->beacon[dup].ap.rssi))) {
                LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Reject duplicate beacon")
                return sky_return(sky_errno, SKY_ERROR_NONE);
            }
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Keep new duplicate beacon %s",
                (b->ap.age == ctx->beacon[dup].ap.age) ? "(stronger signal)" : "(younger)")
            remove_beacon(ctx, dup);
        }
    }

    /* insert the beacon */
    if (insert_beacon(ctx, sky_errno, b, &i) == SKY_ERROR)
        return SKY_ERROR;
    if (is_connected)
        ctx->connected = i;

    /* Update the AP just added to workspace */
    wb = &ctx->beacon[i];
    if (b->h.type == SKY_BEACON_AP) {
        wb->ap.property.in_cache = beacon_in_cache(ctx, b, &idx_cl, &j);
        /* If the added AP is in cache, copy properties */
        if (wb->ap.property.in_cache) {
            cb = &ctx->cache->cacheline[idx_cl].beacon[j];
            wb->ap.property.used = cb->ap.property.used;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "Cached Beacon %-2d:%-2d: WiFi, MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %-4d, %-4d MHz %s",
                idx_cl, j, cb->ap.mac[0], cb->ap.mac[1], cb->ap.mac[2], cb->ap.mac[3],
                cb->ap.mac[4], cb->ap.mac[5], cb->ap.rssi, cb->ap.freq,
                cb->ap.property.used ? "Used" : "Unused")
        } else
            wb->ap.property.used = false;

    } else /* only filter APs */
        return sky_return(sky_errno, SKY_ERROR_NONE);

    /* done if no filtering needed */
    if (ctx->ap_len <= CONFIG(ctx->cache, max_ap_beacons))
        return sky_return(sky_errno, SKY_ERROR_NONE);

    /* beacon is AP and is subject to filtering */
    if (!filter_virtual_aps(ctx))
        if (filter_by_rssi(ctx) == SKY_ERROR) {
            LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "failed to filter")
            return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
        }

    return sky_return(sky_errno, SKY_ERROR_NONE);
}

/*! \brief check if a beacon is in a cache
 *
 *   Scan all beacons in the cache.
 *   the appropriate attributes. If the given beacon is found in the cacheline,
 *   true is returned otherwise false. If index is not NULL, the index of the matching
 *   beacon in the cacheline is saved or -1 if beacon was not found.
 *
 *  @param ctx Skyhook request context
 *  @param b pointer to new beacon
 *  @param idx_cl pointer to where the index of matching cacheline is saved
 *  @param idx_b pointer to where the index of matching beacon is saved
 *
 *  @return true if beacon successfully found or false
 */
static bool beacon_in_cache(Sky_ctx_t *ctx, Beacon_t *b, int *idx_cl, int *idx_b)
{
    int i, j;
    Sky_cacheline_t *cl;

    if (!b || !ctx) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad params")
        return false;
    }

    for (i = 0; i < ctx->cache->len; i++) {
        cl = &ctx->cache->cacheline[i];
        if (cl->time == 0) {
            return false;
        }
        if (beacon_in_cacheline(ctx, b, cl, &j)) {
            if (idx_cl)
                *idx_cl = i;
            if (idx_b)
                *idx_b = j;
            return true;
        }
    }
    return false;
}

/*! \brief check if a beacon is in a cacheline
 *
 *   Scan all beacons in the cacheline. If the type matches the given beacon, compare
 *   the appropriate attributes. If the given beacon is found in the cacheline,
 *   true is returned otherwise false. If index is not NULL, the index of the matching
 *   beacon in the cacheline is saved or -1 if beacon was not found.
 *
 *  @param ctx Skyhook request context
 *  @param b pointer to new beacon
 *  @param cl pointer to cacheline
 *  @param index pointer to where the index of matching beacon is saved
 *
 *  @return true if beacon successfully found or false
 */
static bool beacon_in_cacheline(Sky_ctx_t *ctx, Beacon_t *b, Sky_cacheline_t *cl, int *index)
{
    int j;
    bool ret = false;

    if (!cl || !b || !ctx) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad params")
        return false;
    }

    if (cl->time == 0) {
        return false;
    }

    for (j = 0; j < cl->len; j++) {
        if (b->h.type == cl->beacon[j].h.type) {
            switch (b->h.type) {
            case SKY_BEACON_AP:
                if (memcmp(b->ap.mac, cl->beacon[j].ap.mac, MAC_SIZE) == 0)
                    ret = true;
                break;
            case SKY_BEACON_BLE:
                if ((memcmp(b->ble.mac, cl->beacon[j].ble.mac, MAC_SIZE) == 0) &&
                    (b->ble.major == cl->beacon[j].ble.major) &&
                    (b->ble.minor == cl->beacon[j].ble.minor) &&
                    (memcmp(b->ble.uuid, cl->beacon[j].ble.uuid, 16) == 0))
                    ret = true;
                break;
            case SKY_BEACON_CDMA:
                if ((b->cdma.sid == cl->beacon[j].cdma.sid) &&
                    (b->cdma.nid == cl->beacon[j].cdma.nid) &&
                    (b->cdma.bsid == cl->beacon[j].cdma.bsid))
                    ret = true;
                break;
            case SKY_BEACON_GSM:
                if ((b->gsm.ci == cl->beacon[j].gsm.ci) && (b->gsm.mcc == cl->beacon[j].gsm.mcc) &&
                    (b->gsm.mnc == cl->beacon[j].gsm.mnc) && (b->gsm.lac == cl->beacon[j].gsm.lac))
                    ret = true;
                break;
            case SKY_BEACON_LTE:
                if ((b->lte.e_cellid == cl->beacon[j].lte.e_cellid) &&
                    (b->lte.mcc == cl->beacon[j].lte.mcc) && (b->lte.mnc == cl->beacon[j].lte.mnc))
                    ret = true;
                break;
            case SKY_BEACON_NBIOT:
                if ((b->nbiot.mcc == cl->beacon[j].nbiot.mcc) &&
                    (b->nbiot.mnc == cl->beacon[j].nbiot.mnc) &&
                    (b->nbiot.e_cellid == cl->beacon[j].nbiot.e_cellid) &&
                    (b->nbiot.tac == cl->beacon[j].nbiot.tac))
                    ret = true;
                break;
            case SKY_BEACON_UMTS:
                if ((b->umts.ucid == cl->beacon[j].umts.ucid) &&
                    (b->umts.mcc == cl->beacon[j].umts.mcc) &&
                    (b->umts.mnc == cl->beacon[j].umts.mnc) &&
                    (b->umts.lac == cl->beacon[j].umts.lac))
                    ret = true;
                break;
            default:
                ret = false;
            }
        }
        if (ret == true)
            break;
    }
    if (index)
        *index = (ret == true) ? j : -1;
    return ret;
}

/*! \brief count number of cached APs in workspace relative to a cacheline
 *
 *  @param ctx Skyhook request context
 *  @param cl the cacheline to count in, otherwise count in workspace
 *
 *  @return number of used APs or -1 for fatal error
 */
static int count_aps_in_cacheline(Sky_ctx_t *ctx, Sky_cacheline_t *cl)
{
    int num_aps_cached = 0;
    int j;

    if (!ctx || !cl)
        return -1;
    for (j = 0; j < ctx->ap_len; j++) {
        if (beacon_in_cacheline(ctx, &ctx->beacon[j], cl, NULL))
            num_aps_cached++;
    }
#ifdef VERBOSE_DEBUG
    LOGFMT(
        ctx, SKY_LOG_LEVEL_DEBUG, "%d APs in cache %d", num_aps_cached, cl - ctx->cache->cacheline)
#endif
    return num_aps_cached;
}

/*! \brief count number of used APs in workspace relative to a cacheline
 *
 *  @param ctx Skyhook request context
 *  @param cl the cacheline to count in, otherwise count in workspace
 *
 *  @return number of used APs or -1 for fatal error
 */
static int count_used_aps_in_workspace(Sky_ctx_t *ctx, Sky_cacheline_t *cl)
{
    int num_aps_used = 0;
    int j, index;

    if (!ctx || !cl)
        return -1;
    for (j = 0; j < ctx->ap_len; j++) {
        beacon_in_cacheline(ctx, &ctx->beacon[j], cl, &index);
        if (index != -1 && cl->beacon[index].ap.property.used)
            num_aps_used++;
    }
#ifdef VERBOSE_DEBUG
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%d used APs in workspace", num_aps_used)
#endif
    return num_aps_used;
}

/*! \brief count number of used AP in cachelines
 *
 *  @param ctx Skyhook request context
 *  @param cl the cacheline to count in, otherwise count in workspace
 *
 *  @return number of used APs or -1 for fatal error
 */
static int count_used_aps_in_cacheline(Sky_ctx_t *ctx, Sky_cacheline_t *cl)
{
    int num_aps_used = 0;
    int j;

    if (!ctx || !cl)
        return -1;
    for (j = 0; j < cl->ap_len; j++) {
        if (cl->beacon[j].ap.property.used)
            num_aps_used++;
    }
#ifdef VERBOSE_DEBUG
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%d used APs in cache", num_aps_used)
#endif
    return num_aps_used;
}

/*! \brief find cache entry with a match to workspace
 *
 *   Expire any old cachelines
 *   Compare each cacheline with the workspace beacons:
 *    . If workspace has enough Used APs, compare them with low threshold
 *    . If just a few APs, compare all APs with higher threshold
 *    . If no APs, compare cells for 100% match
 *
 *   If any cacheline score meets threshold, accept it.
 *   While searching, keep track of best cacheline to
 *   save a new server response. An empty cacheline is
 *   best, a good match is next, oldest is the fall back.
 *   Best cacheline for save is saved in the workspace context.
 *
 *  @param ctx Skyhook request context
 *
 *  @return index of best match or empty cacheline or -1
 */
int find_best_match(Sky_ctx_t *ctx)
{
    int i, j, err, score, threshold;
    float ratio, bestratio = -1.0, bestputratio = -1.0;
    int num_aps_used = 0;
    int bestc = -1, bestput = -1;
    int bestthresh = 0;

    dump_cache(ctx);
    dump_workspace(ctx);

    /* expire old cachelines and note first empty cacheline as best line to save to */
    for (i = 0, err = false; i < CACHE_SIZE; i++) {
        /* if cacheline is old, mark it empty */
        if (ctx->cache->cacheline[i].time != 0 &&
            ((uint32_t)(*ctx->gettime)(NULL)-ctx->cache->cacheline[i].time) >
                (CONFIG(ctx->cache, cache_age_threshold) * SECONDS_IN_HOUR)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache line %d expired", i)
            ctx->cache->cacheline[i].time = 0;
        }
        /* if line is empty and it is the first one, remember it */
        if (ctx->cache->cacheline[i].time == 0) {
            if (bestputratio < 1.0) {
                bestput = i;
                bestputratio = 1.0;
            }
        }
    }

    /* score each cache line wrt beacon match ratio */
    for (i = 0, err = false; i < CACHE_SIZE; i++) {
        threshold = ratio = score = 0;
        if (ctx->cache->cacheline[i].time == 0) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score 0 for empty cacheline", i)
            continue;
        } else {
            /* count number of used APs */
            if ((num_aps_used = count_used_aps_in_workspace(ctx, &ctx->cache->cacheline[i])) < 0) {
                err = true;
                break;
            }
            if (num_aps_used) {
                /* there are some significant APs */
                if (num_aps_used < CONFIG(ctx->cache, cache_beacon_threshold)) {
                    /* if there are only a few significant APs, Score based on ALL APs */
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score based on ALL APs", i)

                    if ((score = count_aps_in_cacheline(ctx, &ctx->cache->cacheline[i])) < 0) {
                        err = true;
                        break;
                    }
                    int unionAB = (ctx->ap_len +
                                   MIN(ctx->cache->cacheline[i].ap_len,
                                       CONFIG(ctx->cache, max_ap_beacons)) -
                                   score);
                    threshold = 33 + (2 * CONFIG(ctx->cache, cache_match_threshold)) / 3;
                    ratio = (float)score / unionAB;
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: score %d (%d/%d) vs %d", i,
                        (int)round(ratio * 100), score, unionAB, threshold)
                } else {
                    /* there are are enough significant APs, Score based on just Used APs */
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score based on just Used APs", i)

                    int unionAB = count_used_aps_in_cacheline(ctx, &ctx->cache->cacheline[i]);
                    if (unionAB < 0) {
                        err = true;
                        break;
                    }
                    ratio = (float)num_aps_used / unionAB;
                    threshold = CONFIG(ctx->cache, cache_match_threshold);
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: score %d (%d/%d) vs %d", i,
                        (int)round(ratio * 100), num_aps_used, unionAB, threshold)
                }
            } else {
                /* score cachelines based on cell */
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache %d Score based on cell", i)

                for (j = ctx->ap_len; j < ctx->len; j++) {
                    if (beacon_in_cacheline(
                            ctx, &ctx->beacon[j], &ctx->cache->cacheline[i], NULL)) {
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache %d: cell %d type %s matches", i, j,
                            sky_pbeacon(&ctx->beacon[j]))
                        score = score + 1;
                    }
                }
                ratio = (score == ctx->len - ctx->ap_len) ? 100.0 : 0.0;
                threshold = CONFIG(ctx->cache, cache_match_threshold);
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: Score %d (%d/%d) vs %d", i,
                    (int)round(ratio * 100), score, ctx->len - ctx->ap_len, threshold)
            }
        }
        if (ratio > bestputratio) {
            bestput = i;
            bestputratio = ratio;
        }
        if (ratio > bestratio) {
            if (bestratio > 0.0)
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    "Found better match in cache %d of 0..%d score %d (vs %d)", i, CACHE_SIZE - 1,
                    (int)round(ratio * 100), threshold)
            bestc = i;
            bestratio = ratio;
            bestthresh = threshold;
        }
        if (ratio * 100 > threshold)
            break;
    }
    if (err) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad parameters counting APs")
        return -1;
    }

    /* make a note of the best match used by add_to_cache */
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Best cacheline for put to cache: %d of 0..%d score %d",
        bestput, CACHE_SIZE - 1, (int)round(bestputratio * 100))
    ctx->bestput = bestput;

    if (bestratio * 100 > bestthresh) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "location in cache, pick cache %d of 0..%d score %d (vs %d)", bestc, CACHE_SIZE - 1,
            (int)round(bestratio * 100), bestthresh)
        return bestc;
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache match failed. Cache %d, best score %d (vs %d)", bestc,
        (int)round(bestratio * 100), bestthresh)
    return -1;
}

/*! \brief find cache entry with oldest entry
 *
 *  @param ctx Skyhook request context
 *
 *  @return index of oldest cache entry, or empty
 */
static int find_oldest(Sky_ctx_t *ctx)
{
    int i;
    uint32_t oldestc = 0;
    int oldest = (*ctx->gettime)(NULL);

    for (i = 0; i < CACHE_SIZE; i++) {
        if (ctx->cache->cacheline[i].time == 0)
            return i;
        else if (ctx->cache->cacheline[i].time < oldest) {
            oldest = ctx->cache->cacheline[i].time;
            oldestc = i;
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cacheline %d oldest time %d", oldestc, oldest)
    return oldestc;
}

/*! \brief add location to cache
 *
 *   The location is saved in the cacheline indicated by bestput (set by find_best_match)
 *   unless this is -1, in which case, location is saved in oldest cacheline.
 *
 *  @param ctx Skyhook request context
 *  @param loc pointer to location info
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
Sky_status_t add_to_cache(Sky_ctx_t *ctx, Sky_location_t *loc)
{
    int i = ctx->bestput;
    int j;
    uint32_t now = (*ctx->gettime)(NULL);
    Sky_cacheline_t *cl;

    if (CACHE_SIZE < 1) {
        return SKY_SUCCESS;
    }

    /* compare current time to Mar 1st 2019 */
    if (now <= TIMESTAMP_2019_03_01) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Don't have good time of day!")
        return SKY_ERROR;
    }

    /* Find best match in cache */
    /*    yes - add entry here */
    /* else find oldest cache entry */
    /*    yes - add entryu here */
    if (i < 0) {
        i = find_oldest(ctx);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "find_oldest chose cache %d of 0..%d", i, CACHE_SIZE - 1)
    }
    cl = &ctx->cache->cacheline[i];
    if (loc->location_status != SKY_LOCATION_STATUS_SUCCESS) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Won't add unknown location to cache")
        cl->time = 0; /* clear cacheline */
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "clearing cache %d of 0..%d", i, CACHE_SIZE - 1)
        return SKY_ERROR;
    } else if (cl->time == 0)
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Saving to empty cache %d of 0..%d", i, CACHE_SIZE - 1)
    else
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Saving to cache %d of 0..%d", i, CACHE_SIZE - 1)
    cl->len = ctx->len;
    cl->ap_len = ctx->ap_len;
    cl->loc = *loc;
    cl->time = now;
    for (j = 0; j < CONFIG(ctx->cache, total_beacons); j++)
        cl->beacon[j] = ctx->beacon[j];
    return SKY_SUCCESS;
}

/*! \brief get location from cache
 *
 *  @param ctx Skyhook request context
 *  @param best Where to save best match, can be used to save to cache
 *
 *  @return cacheline index or -1
 */
int get_from_cache(Sky_ctx_t *ctx)
{
    uint32_t now = (*ctx->gettime)(NULL);

    if (CACHE_SIZE < 1) {
        return SKY_ERROR;
    }

    /* compare current time to Mar 1st 2019 */
    if (now <= TIMESTAMP_2019_03_01) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Don't have good time of day!")
        return SKY_ERROR;
    }
    return find_best_match(ctx);
}
