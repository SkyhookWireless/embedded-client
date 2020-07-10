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

#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define NOMINAL_RSSI(b) ((b) == -1 ? (-90) : (b))
#define PUT_IN_CACHE true
#define GET_FROM_CACHE false

void dump_workspace(Sky_ctx_t *ctx);
void dump_cache(Sky_ctx_t *ctx);
static bool beacon_in_cache(Sky_ctx_t *ctx, Beacon_t *b, Sky_cacheline_t *cl);

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
        if (ctx->connected == index)
            ctx->connected = -1;
        else if (index < ctx->connected)
            // Removed beacon precedes the connected one, so update its index.
            ctx->connected--;

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

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon type %s inserted idx: %d", sky_pbeacon(b), i);

    if (i <= ctx->connected)
        // New beacon was inserted before the connected one, so update its index.
        ctx->connected++;

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

    if (NUM_APS(ctx) <= CONFIG(ctx->cache, max_ap_beacons))
        return SKY_ERROR;

    /* what share of the range of rssi values does each beacon represent */
    band_range = (NOMINAL_RSSI(ctx->beacon[NUM_APS(ctx) - 1].ap.rssi) -
                     NOMINAL_RSSI(ctx->beacon[0].ap.rssi)) /
                 ((float)NUM_APS(ctx) - 1);

    /* if the rssi range is small, throw away middle beacon */

    if (band_range < 0.5) {
        /* search from middle of range looking for beacon not in cache */
        for (jump = 0, up_down = -1, i = NUM_APS(ctx) / 2; i >= 0 && i < NUM_APS(ctx);
             jump++, i += up_down * jump, up_down = -up_down) {
            if (!ctx->beacon[i].ap.in_cache) {
                LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small. %s beacon",
                    !jump ? "Remove middle" : "Found non-cached")
                return remove_beacon(ctx, i);
            }
        }
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small. Removing cached beacon")
        return remove_beacon(ctx, NUM_APS(ctx) / 2);
    }

    /* if beacon with min RSSI is below threshold, throw it out */
    if (NOMINAL_RSSI(ctx->beacon[0].ap.rssi) < -CONFIG(ctx->cache, cache_neg_rssi_threshold)) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Discarding beacon %d with very weak strength", 0)
        return remove_beacon(ctx, 0);
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
        if (!ctx->beacon[i].ap.in_cache &&
            fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]) > worst) {
            worst = fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]);
            reject = i;
        }
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons */
        /* Throw away either lowest or highest rssi valued beacons */
        if (!ctx->beacon[NUM_APS(ctx) - 1].ap.in_cache && ctx->beacon[0].ap.in_cache)
            reject = NUM_APS(ctx) - 1;
        else
            reject = 0; /* Throw away lowest rssi value */
    }
#if SKY_DEBUG
    for (i = 0; i < NUM_APS(ctx); i++) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%s: %-2d, %s ideal %d.%02d fit %2d.%02d (%d)",
            (reject == i) ? "remove" : "      ", i,
            ctx->beacon[i].ap.in_cache ? "cached" : "      ", (int)ideal_rssi[i],
            (int)fabs(round(100 * (ideal_rssi[i] - (int)ideal_rssi[i]))),
            (int)fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]), ideal_rssi[i],
            (int)fabs(
                round(100 * (fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i]) -
                                (int)fabs(NOMINAL_RSSI(ctx->beacon[i].ap.rssi) - ideal_rssi[i])))),
            ctx->beacon[i].ap.rssi)
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
        (int)ctx->ap_len, (int)ctx->len)

    dump_workspace(ctx);

    if (ctx->beacon[0].h.type != SKY_BEACON_AP) {
        LOGFMT(ctx, SKY_LOG_LEVEL_CRITICAL, "beacon type not WiFi")
        return false;
    }

    /* remove an AP beacon that is 'similar' to the one just added */
    /* walk through all beacons in workspace (ignoring the one just added) */
    for (i = 0; i < ctx->ap_len; i++) {
        if (i == b)
            continue;

        if ((cmp = similar(ctx->beacon[i].ap.mac, ctx->beacon[b].ap.mac)) < 0) {
            if (ctx->beacon[b].ap.in_cache) {
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
            if (ctx->beacon[i].ap.in_cache) {
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
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d similar to %d%s", rm, keep,
                cached ? " (cached)" : "")
            remove_beacon(ctx, rm);
            return true;
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "no match")
    return false;
}

/*! \brief add beacon to list
 *  if beacon is AP, filter
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
    int i = -1;
    int dup = -1;

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

    if (b->h.type == SKY_BEACON_AP)
        ctx->beacon[i].ap.in_cache =
            beacon_in_cache(ctx, b, &ctx->cache->cacheline[ctx->cache->newest]);
    else /* only filter APs */
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
 *  @param ctx Skyhook request context
 *  @param b pointer to new beacon
 *  @param cl pointer to cacheline
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
static bool beacon_in_cache(Sky_ctx_t *ctx, Beacon_t *b, Sky_cacheline_t *cl)
{
    int j;
    bool ret = false;

    if (!cl || !b || !ctx) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad params");
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
        }
    return ret;
}

/*! \brief find cache entry with best match
 *  if beacon is AP, filter
 *
 *  @param ctx Skyhook request context
 *  @param put boolean which reflects put or get from cache
 *
 *  @return index of best match or empty cacheline or -1
 */
int find_best_match(Sky_ctx_t *ctx, bool put)
{
    Sky_cacheline_t *cl;
    int i, j, start, end;
    float ratio[CACHE_SIZE];
    int score[CACHE_SIZE];
    float bestratio = 0;
    int bestscore = 0;
    int bestc = -1;

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%s", put ? "for save to cache" : "for get from cache")
    dump_workspace(ctx);

    /* score each cache line wrt beacon match ratio */
    for (i = 0; i < CACHE_SIZE; i++) {
        cl = &ctx->cache->cacheline[i];
        ratio[i] = score[i] = 0;
        if (cl->time != 0 && ((uint32_t)(*ctx->gettime)(NULL)-cl->time) >
                                 (CONFIG(ctx->cache, cache_age_threshold) * SECONDS_IN_HOUR)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache line %d expired", i);
            cl->time = 0;
        }
        if (put && (cl->time == 0)) {
            /* looking for match for put, empty cacheline = 1st choice. Mark this special case */
            score[i] = CONFIG(ctx->cache, total_beacons) * 2;
        } else if (!put && cl->time == 0) {
            /* looking for match for get, ignore empty cache */
            continue;
        } else {
            if (NUM_APS(ctx)) {
                /* Non empty cacheline - count matching AP beacons */
                start = 0;
                end = NUM_APS(ctx);
            } else {
                /* Non empty cacheline - count matching Cell beacons */
                start = NUM_APS(ctx);
                end = NUM_BEACONS(ctx);
            }

            for (j = start; j < end; j++) {
                if (beacon_in_cache(ctx, &ctx->beacon[j], cl)) {
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon %d type %s matches cache %d of 0..%d",
                        j, sky_pbeacon(&ctx->beacon[j]), i, CACHE_SIZE)
                    score[i] = score[i] + 1;
                }
            }
        }
    }

    for (i = 0; i < CACHE_SIZE; i++) {
        cl = &ctx->cache->cacheline[i];
        /* Special case - empty cacheline noted above */
        if (score[i] == CONFIG(ctx->cache, total_beacons) * 2) {
            ratio[i] = 1.0;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: fill empty cacheline", i)
        } else if (NUM_APS(ctx) && NUM_APS(cl)) { /* APs present */
            // if cell in workspace and no match in cacheline then score = 0
            if (NUM_CELLS(ctx) &&
                IMPLIES(NUM_CELLS(cl), !beacon_in_cache(ctx, &ctx->beacon[NUM_APS(ctx)], cl))) {
                ratio[i] = 0.0;
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: Cell changed score 0", i)
            } else {
                // score APs = intersection(A, B) / union(A, B)
                int unionAB = (NUM_APS(ctx) + MIN(NUM_APS(cl), CONFIG(ctx->cache, max_ap_beacons)) -
                               score[i]);
                ratio[i] = (float)score[i] / unionAB;
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: score %d (%d/%d)", i,
                    (int)round(ratio[i] * 100), score[i], unionAB)
            }
        } else if (NUM_CELLS(ctx) && NUM_CELLS(cl)) { /* Check cell beacons */
            // if all cell beacons match, score = 100
            ratio[i] = (score[i] == NUM_CELLS(ctx)) ? 1.0 : 0.0;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: Of %d cell beacons %d match", i,
                NUM_CELLS(ctx), score[i])
        }

        if (ratio[i] > bestratio) {
            bestratio = ratio[i];
            bestscore = score[i];
            bestc = i;
        }
    }

    /* if match is for get, must meet threshold */
    if (!put) {
        if (NUM_APS(ctx) <= CONFIG(ctx->cache, cache_beacon_threshold) &&
            bestscore == NUM_APS(ctx)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Only %d APs; pick cache %d of 0..%d score %d (vs %d)",
                NUM_APS(ctx), bestc, CACHE_SIZE, (int)round(ratio[bestc] * 100),
                CONFIG(ctx->cache, cache_beacon_threshold))
            return bestc;
        } else if (NUM_APS(ctx) > CONFIG(ctx->cache, cache_beacon_threshold) &&
                   bestratio * 100 > CONFIG(ctx->cache, cache_match_threshold)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "location in cache, pick cache %d of 0..%d score %d (vs %d)", bestc, CACHE_SIZE - 1,
                (int)round(ratio[bestc] * 100), CONFIG(ctx->cache, cache_match_threshold))
            return bestc;
        }
    } else if (bestc >= 0) { /* match is for put */
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "save location in best cache, %d of 0..%d score %d (vs %d)", bestc, CACHE_SIZE - 1,
            (int)round(ratio[bestc] * 100), CONFIG(ctx->cache, cache_match_threshold))
        return bestc;
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache match failed. Best score %d (vs %d)",
        (int)round(bestratio * 100), CONFIG(ctx->cache, cache_match_threshold))
    return -1;
}

/*! \brief find cache entry with oldest entry
 *  if beacon is AP, filter
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
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cacheline %d oldest time %d", oldestc, oldest);
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
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cacheline %d is newest", idx);
    }
}

/*! \brief add location to cache
 *
 *  @param ctx Skyhook request context
 *  @param loc pointer to location info
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
Sky_status_t add_to_cache(Sky_ctx_t *ctx, Sky_location_t *loc)
{
    int i = -1;
    int j;
    uint32_t now = (*ctx->gettime)(NULL);

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
    if ((i = find_best_match(ctx, PUT_IN_CACHE)) < 0) {
        i = find_oldest(ctx);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "find_oldest chose cache %d of 0..%d", i, CACHE_SIZE)
    } else {
        if (loc->location_status != SKY_LOCATION_STATUS_SUCCESS) {
            ctx->cache->cacheline[i].time = 0; /* clear cacheline */
            update_newest_cacheline(ctx);
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "find_best_match found cache match %d of 0..%d, but cleared", i, CACHE_SIZE);
        } else
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "find_best_match found cache match %d of 0..%d", i,
                CACHE_SIZE);
    }
    if (loc->location_status != SKY_LOCATION_STATUS_SUCCESS) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Won't add unknown location to cache")
        return SKY_ERROR;
    }
    ctx->cache->cacheline[i].len = NUM_BEACONS(ctx);
    ctx->cache->cacheline[i].ap_len = NUM_APS(ctx);
    ctx->cache->cacheline[i].loc = *loc;
    ctx->cache->cacheline[i].time = now;
    ctx->cache->newest = i;
    for (j = 0; j < CONFIG(ctx->cache, total_beacons); j++)
        ctx->cache->cacheline[i].beacon[j] = ctx->beacon[j];
    return SKY_SUCCESS;
}

/*! \brief get location from cache
 *
 *  @param ctx Skyhook request context
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
    return find_best_match(ctx, GET_FROM_CACHE);
}
