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
#define SKY_LIBEL 1
#include "libel.h"

/* #define VERBOSE_DEBUG 1 */

#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define NOMINAL_RSSI(b) ((b) == -1 ? (-90) : (b))
#define PUT_IN_CACHE true
#define GET_FROM_CACHE false

void dump_workspace(Sky_ctx_t *ctx);
void dump_cache(Sky_ctx_t *ctx);
static bool beacon_in_cache(Sky_ctx_t *ctx, Beacon_t *b, Sky_cacheline_t *cl, int *index);

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
    if (index >= NUM_BEACONS(ctx))
        return SKY_ERROR;

    if (ctx->beacon[index].h.type == SKY_BEACON_AP)
        NUM_APS(ctx) -= 1;

    memmove(&ctx->beacon[index], &ctx->beacon[index + 1],
        sizeof(Beacon_t) * (NUM_BEACONS(ctx) - index - 1));
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "idx:%d", index)
    NUM_BEACONS(ctx) -= 1;
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
    for (i = 0; i < NUM_BEACONS(ctx); i++)
        if (ctx->beacon[i].h.type >= b->h.type)
            break;

    /* add beacon at the end */
    if (i == NUM_BEACONS(ctx)) {
        ctx->beacon[i] = *b;
        NUM_BEACONS(ctx)++;
    } else {
        /* if AP, add in rssi order */
        if (b->h.type == SKY_BEACON_AP) {
            for (; i < NUM_APS(ctx); i++)
                if (ctx->beacon[i].h.type != SKY_BEACON_AP ||
                    NOMINAL_RSSI(ctx->beacon[i].ap.rssi) > NOMINAL_RSSI(b->ap.rssi))
                    break;
        }
        /* shift beacons to make room for the new one */
        memmove(&ctx->beacon[i + 1], &ctx->beacon[i], sizeof(Beacon_t) * (NUM_BEACONS(ctx) - i));
        ctx->beacon[i] = *b;
        NUM_BEACONS(ctx)++;
    }
    /* report back the position beacon was added */
    if (index != NULL)
        *index = i;
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon type %s inserted idx: %d", sky_pbeacon(b), i)

    if (b->h.type == SKY_BEACON_AP)
        NUM_APS(ctx)++;
    return SKY_SUCCESS;
}

/*! \brief try to remove one AP by selecting an AP which leaves best spread of rssi values
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
    Beacon_t *b;

    if (NUM_APS(ctx) <= CONFIG(ctx->cache, max_ap_beacons))
        return SKY_ERROR;

    /* what share of the range of rssi values does each beacon represent */
    band_range = (NOMINAL_RSSI(ctx->beacon[NUM_APS(ctx) - 1].ap.rssi) -
                     NOMINAL_RSSI(ctx->beacon[0].ap.rssi)) /
                 ((float)NUM_APS(ctx) - 1);

    /* if the rssi range is small, throw away middle beacon */

    if (band_range < 0.5) {
        /* search from middle of range looking for Unused beacon in cache */
        for (jump = 0, up_down = -1, i = NUM_APS(ctx) / 2; i >= 0 && i < NUM_APS(ctx);
             jump++, i += up_down * jump, up_down = -up_down) {
            b = &ctx->beacon[i];
            if (!b->ap.property.in_cache || (b->ap.property.in_cache && !b->ap.property.used)) {
                LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small. %s beacon",
                    !jump ? "Remove middle Unused" : "Found Unused")
                return remove_beacon(ctx, i);
            }
        }
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small. Removing cached beacon")
        return remove_beacon(ctx, NUM_APS(ctx) / 2);
    }

    /* if beacon with min RSSI is below threshold, throw out weak one, not in cache or Unused */
    if (NOMINAL_RSSI(ctx->beacon[0].ap.rssi) < -CONFIG(ctx->cache, cache_neg_rssi_threshold)) {
        for (i = 0, reject = -1; i < NUM_APS(ctx) && reject == -1; i++) {
            if (ctx->beacon[i].ap.rssi < -CONFIG(ctx->cache, cache_neg_rssi_threshold) &&
                !ctx->beacon[i].ap.property.in_cache)
                reject = i;
        }
        for (i = 0; i < NUM_APS(ctx) && reject == -1; i++) {
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
        (NOMINAL_RSSI(ctx->beacon[NUM_APS(ctx) - 1].ap.rssi) -
            NOMINAL_RSSI(ctx->beacon[0].ap.rssi)),
        (int)band_range, (int)fabs(round(100 * (band_range - (int)band_range))))

    /* for each beacon, work out it's ideal rssi value to give an even distribution */
    for (i = 0; i < NUM_APS(ctx); i++)
        ideal_rssi[i] = NOMINAL_RSSI(ctx->beacon[0].ap.rssi) + (i * band_range);

    /* find AP with poorest fit to ideal rssi */
    /* always keep lowest and highest rssi */
    /* unless all the middle candidates are in the cache */
    for (i = 1, reject = -1, worst = 0; i < NUM_APS(ctx) - 1; i++) {
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
        else if (!ctx->beacon[NUM_APS(ctx) - 1].ap.property.in_cache)
            reject = NUM_APS(ctx) - 1;
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons */
        /* Throw away Unused beacon with worst fit */
        for (i = 1, worst = 0; i < NUM_APS(ctx) - 1; i++) {
            b = &ctx->beacon[i];
            if (!b->ap.property.used && fabs(NOMINAL_RSSI(b->ap.rssi) - ideal_rssi[i]) > worst) {
                worst = fabs(NOMINAL_RSSI(b->ap.rssi) - ideal_rssi[i]);
                reject = i;
            }
        }
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons and no Unused */
        /* Throw away either lowest or highest rssi valued beacons if not Used */
        if (!ctx->beacon[0].ap.property.used)
            reject = 0;
        else if (!ctx->beacon[NUM_APS(ctx) - 1].ap.property.used)
            reject = NUM_APS(ctx) - 1;
        else
            reject =
                NUM_APS(ctx) / 2; /* remove middle beacon (all beacons are in cache and Used) */
    }
#if SKY_DEBUG
    for (i = 0; i < NUM_APS(ctx); i++) {
        b = &ctx->beacon[i];
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%s: %-2d, %s ideal %d.%02d fit %2d.%02d (%d)",
            (reject == i) ? "remove" : "      ", i,
            b->ap.property.in_cache ? b->ap.property.used ? "Used  " : "Unused" : "      ",
            (int)ideal_rssi[i], (int)fabs(round(100 * (ideal_rssi[i] - (int)ideal_rssi[i]))),
            (int)fabs(NOMINAL_RSSI(b->ap.rssi) - ideal_rssi[i]), ideal_rssi[i],
            (int)fabs(round(100 * (fabs(NOMINAL_RSSI(b->ap.rssi) - ideal_rssi[i]) -
                                      (int)fabs(NOMINAL_RSSI(b->ap.rssi) - ideal_rssi[i])))),
            b->ap.rssi)
    }
#endif
    return remove_beacon(ctx, reject);
}

/*! \brief remove an AP if there is a similar one to the one just added
 *         When similar, remove beacon with highest mac address
 *         unless it is in cache, then choose to remove the uncached beacon
 *
 *  @param ctx Skyhook request context
 *  @param b index of beacon just added
 *
 *  @return true if beacon removed or false otherwise
 */
static bool remove_virtual_ap(Sky_ctx_t *ctx, int b)
{
    int i;
    int cmp, rm = -1;
#if SKY_DEBUG
    int keep = -1;
    bool cached = false;
#endif

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "just added beacon at idx %d, ap_len: %d APs of %d beacons", b,
        (int)NUM_APS(ctx), (int)NUM_BEACONS(ctx))

    if (ctx->beacon[0].h.type != SKY_BEACON_AP) {
        LOGFMT(ctx, SKY_LOG_LEVEL_CRITICAL, "beacon type not WiFi")
        return false;
    }
    i = b;
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
        " Beacon %-2d: WiFi Age: %d %s MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %-4d %-4d MHz", i,
        ctx->beacon[i].ap.age,
        ctx->beacon[i].ap.property.in_cache ?
            (ctx->beacon[i].ap.property.used ? "Used  " : "Unused") :
            "      ",
        ctx->beacon[i].ap.mac[0], ctx->beacon[i].ap.mac[1], ctx->beacon[i].ap.mac[2],
        ctx->beacon[i].ap.mac[3], ctx->beacon[i].ap.mac[4], ctx->beacon[i].ap.mac[5],
        ctx->beacon[i].ap.rssi, ctx->beacon[i].ap.freq)

    /* remove an AP beacon that is 'similar' to the one just added */
    /* walk through all beacons in workspace (ignoring the one just added) */
    for (i = 0; i < NUM_APS(ctx); i++) {
        if (i == b)
            continue;

        if ((cmp = similar(ctx->beacon[i].ap.mac, ctx->beacon[b].ap.mac)) < 0) {
            if (ctx->beacon[b].ap.property.in_cache) {
                rm = i;
#if SKY_DEBUG
                keep = b;
                cached = true;
#endif
            } else {
                rm = b;
#if SKY_DEBUG
                keep = i;
#endif
            }
        } else if (cmp > 0) {
            if (ctx->beacon[i].ap.property.in_cache) {
                rm = b;
#if SKY_DEBUG
                keep = i;
                cached = true;
#endif
            } else {
                rm = i;
#if SKY_DEBUG
                keep = b;
#endif
            }
        }
        if (rm != -1) {
            dump_workspace(ctx);
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d similar to %d%s", rm, keep,
                cached ? " (cached)" : "")
            remove_beacon(ctx, rm);
            return true;
        }
    }
    // LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "no match")
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
    int i = -1, j = 0;
    int dup = -1;
    Beacon_t *w, *c;

    /* don't add any more non-AP beacons if we've already hit the limit of non-AP beacons */
    if (b->h.type != SKY_BEACON_AP &&

        NUM_CELLS(ctx) > (CONFIG(ctx->cache, total_beacons) - CONFIG(ctx->cache, max_ap_beacons))) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Too many cell beacons (%s ignored)", sky_pbeacon(b))
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);
    } else if (b->h.type == SKY_BEACON_AP) {
        if (!validate_mac(b->ap.mac, ctx))
            return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
        /* see if this mac already added (duplicate beacon) */
        for (dup = 0; dup < NUM_APS(ctx); dup++) {
            if (memcmp(b->ap.mac, ctx->beacon[dup].ap.mac, MAC_SIZE) == 0) {
                break;
            }
        }
        /* if it is already in workspace */
        if (dup < NUM_APS(ctx)) {
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
    w = &ctx->beacon[i];
    if (b->h.type == SKY_BEACON_AP) {
        w->ap.property.in_cache =
            beacon_in_cache(ctx, b, &ctx->cache->cacheline[ctx->cache->newest], &j);
        /* If the added AP is in cache, copy properties */
        c = &ctx->cache->cacheline[ctx->cache->newest].beacon[j];
        if (w->ap.property.in_cache) {
            w->ap.property.used = c->ap.property.used;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "Cached Beacon %-2d: WiFi Age: %d %s MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %-4d %-4d MHz",
                j, c->ap.age, c->ap.property.used ? "Used  " : "Unused", c->ap.mac[0], c->ap.mac[1],
                c->ap.mac[2], c->ap.mac[3], c->ap.mac[4], c->ap.mac[5], c->ap.rssi, c->ap.freq)
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "Worksp Beacon %-2d: WiFi Age: %d %s MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %-4d %-4d MHz",
                i, w->ap.age,
                w->ap.property.in_cache ? (w->ap.property.used ? "Used  " : "Unused") : "      ",
                w->ap.mac[0], w->ap.mac[1], w->ap.mac[2], w->ap.mac[3], w->ap.mac[4], w->ap.mac[5],
                w->ap.rssi, w->ap.freq)
        } else
            w->ap.property.used = false;

    } else /* only filter APs */
        return sky_return(sky_errno, SKY_ERROR_NONE);

    /* discard virtual duplicates */
    remove_virtual_ap(ctx, i);

    /* done if no filtering needed */
    if (NUM_APS(ctx) <= CONFIG(ctx->cache, max_ap_beacons))
        return sky_return(sky_errno, SKY_ERROR_NONE);

    /* beacon is AP and is subject to filtering */
    if (filter_by_rssi(ctx) == SKY_ERROR) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "failed to filter")
        return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
    }

    return sky_return(sky_errno, SKY_ERROR_NONE);
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
static bool beacon_in_cache(Sky_ctx_t *ctx, Beacon_t *b, Sky_cacheline_t *cl, int *index)
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

    /* score each cache line wrt beacon match ratio */
    for (j = 0; ret == false && j < NUM_BEACONS(cl); j++)
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
            if (ret == true)
                break; // for loop without incrementing j
        }
    if (index) {
        *index = (ret == true) ? j : -1;
    }
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
        if (beacon_in_cache(ctx, &ctx->beacon[j], cl, NULL))
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
        beacon_in_cache(ctx, &ctx->beacon[j], cl, &index);
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
    int i; /* i iterates through cacheline */
    int j; /* j iterates through beacons of a cacheline */
    int err; /* err breaks the seach due to bad value */
    float ratio; /* 0.0 <= ratio <= 1.0 is the degree to which workspace matches cacheline
                    In typical case this is the intersection(workspace, cache) / union(workspace, cache) */
    float bestratio = -1.0;
    float bestputratio = -1.0;
    int score; /* score is number of APs found in cacheline */
    int threshold; /* the threshold determined that ratio should meet */
    int num_aps_used = 0;
    int bestc = -1, bestput = -1;
    int bestthresh = 0;
    Sky_cacheline_t *cl;

    dump_cache(ctx);
    dump_workspace(ctx);

    /* expire old cachelines and note first empty cacheline as best line to save to */
    for (i = 0, err = false; i < CACHE_SIZE; i++) {
        cl = &ctx->cache->cacheline[i];
        /* if cacheline is old, mark it empty */
        if (cl->time != 0 && ((uint32_t)(*ctx->gettime)(NULL)-cl->time) >
                                 (CONFIG(ctx->cache, cache_age_threshold) * SECONDS_IN_HOUR)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache line %d expired", i)
            cl->time = 0;
        }
        /* if line is empty and it is the first one, remember it */
        if (cl->time == 0) {
            if (bestputratio < 1.0) {
                bestput = i;
                bestputratio = 1.0;
            }
        }
    }

    /* score each cache line wrt beacon match ratio */
    for (i = 0, err = false; i < CACHE_SIZE; i++) {
        cl = &ctx->cache->cacheline[i];
        threshold = ratio = score = 0;
        if (cl->time == 0) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score 0 for empty cacheline", i)
            continue;
        } else {
            /* count number of used APs */
            if ((num_aps_used = count_used_aps_in_workspace(ctx, cl)) < 0) {
                err = true;
                break;
            }
            if (num_aps_used) {
                /* there are some significant APs */
                if (num_aps_used < CONFIG(ctx->cache, cache_beacon_threshold)) {
                    /* if there are only a few significant APs, Score based on ALL APs */
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score based on ALL APs", i)

                    if ((score = count_aps_in_cacheline(ctx, cl)) < 0) {
                        err = true;
                        break;
                    }
                    int unionAB = (NUM_APS(ctx) +
                                   MIN(NUM_APS(cl), CONFIG(ctx->cache, max_ap_beacons)) - score);
                    threshold = CONFIG(ctx->cache, cache_match_all_threshold);
                    ratio = (float)score / unionAB;
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: score %d (%d/%d) vs %d", i,
                        (int)round(ratio * 100), score, unionAB, threshold)
                } else {
                    /* there are are enough significant APs, Score based on just Used APs */
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score based on just Used APs", i)

                    int unionAB = count_used_aps_in_cacheline(ctx, cl);
                    if (unionAB < 0) {
                        err = true;
                        break;
                    }
                    ratio = (float)num_aps_used / unionAB;
                    threshold = CONFIG(ctx->cache, cache_match_used_threshold);
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: score %d (%d/%d) vs %d", i,
                        (int)round(ratio * 100), num_aps_used, unionAB, threshold)
                }
            } else {
                /* score cachelines based on cell */
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache %d Score based on cell", i)

                for (j = NUM_APS(ctx); j < NUM_BEACONS(ctx); j++) {
                    if (beacon_in_cache(ctx, &ctx->beacon[j], cl, NULL)) {
                        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache %d: cell %d type %s matches", i, j,
                            sky_pbeacon(&ctx->beacon[j]))
                        score = score + 1;
                    }
                }
                ratio = (score == NUM_CELLS(ctx)) ? 1.0 : 0.0;
                threshold = 50;
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: Score %d (%d/%d) vs %d", i,
                    (int)round(ratio * 100), score, NUM_CELLS(ctx), threshold)
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

/*! \brief note newest cache entry
 *
 *  @param ctx Skyhook request context
 *
 *  @return void
 */
static void update_newest_cacheline(Sky_ctx_t *ctx)
{
    int i;
    int newest = 0, idx = 0;

    for (i = 0; i < CACHE_SIZE; i++) {
        if (ctx->cache->cacheline[i].time > newest) {
            newest = ctx->cache->cacheline[i].time;
            idx = i;
        }
    }
    if (newest) {
        ctx->cache->newest = idx;
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cacheline %d is newest", idx)
    }
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
        update_newest_cacheline(ctx);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "clearing cache %d of 0..%d", i, CACHE_SIZE - 1)
        return SKY_ERROR;
    } else if (cl->time == 0)
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Saving to empty cache %d of 0..%d", i, CACHE_SIZE - 1)
    else
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Saving to cache %d of 0..%d", i, CACHE_SIZE - 1)
    cl->len = NUM_BEACONS(ctx);
    cl->ap_len = NUM_APS(ctx);
    cl->loc = *loc;
    cl->time = now;
    ctx->cache->newest = i;

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
