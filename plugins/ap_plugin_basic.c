/*! \file plugins/ap_plugin_basic.c
     *  \brief AP plugin supporting basic APs and cells Only
 *  Plugin for Skyhook Embedded Library
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
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#define SKY_LIBEL
#include "libel.h"

/* Uncomment VERBOSE_DEBUG to enable extra logging */
// #define VERBOSE_DEBUG

#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define EFFECTIVE_RSSI(b) ((b) == -1 ? (-127) : (b))
#define AP_BELOW_RSSI_THRESHOLD(ctx, idx)                                                          \
    (EFFECTIVE_RSSI((ctx)->beacon[(idx)].h.rssi) <                                                 \
        -(int)CONFIG((ctx)->state, cache_neg_rssi_threshold))

/*! \brief compare beacons for equality
 *
 *  if beacons are equivalent, return SKY_SUCCESS otherwise SKY_FAILURE
 *  if an error occurs during comparison. return SKY_ERROR
 */
static Sky_status_t equal(Sky_ctx_t *ctx, Beacon_t *a, Beacon_t *b, Sky_beacon_property_t *prop)
{
    if (!ctx || !a || !b) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad params");
        return SKY_ERROR;
    }

#ifdef VERBOSE_DEBUG
    dump_beacon(ctx, "a:", a, __FILE__, __FUNCTION__);
    dump_beacon(ctx, "b:", b, __FILE__, __FUNCTION__);
#endif
    /* Two APs can be compared but others are ordered by type */
    if (a->h.type != SKY_BEACON_AP || b->h.type != SKY_BEACON_AP)
        return SKY_ERROR;

    /* test two APs for equivalence */
    if (memcmp(a->ap.mac, b->ap.mac, MAC_SIZE) == 0) {
        if (prop != NULL && b->ap.property.in_cache)
            prop->in_cache = true;
        return SKY_SUCCESS;
    }
    return SKY_FAILURE;
}

/*! \brief test two MAC addresses for being members of same virtual Group
 *
 *   Similar means the two mac addresses differ only in one nibble AND
 *   if that nibble is the second-least-significant bit of second hex digit,
 *   then that bit must match too.
 *
 *  @param macA pointer to the first MAC
 *  @param macB pointer to the second MAC
 *  @param pn pointer to nibble index of where they differ if similar (0-11)
 *
 *  @return negative, 0 or positive
 *  return 0 when NOT similar, negative indicates parent is B, positive parent is A
 *  if macs are similar, and pn is not NULL, *pn is set to nibble index of difference
 */
static int mac_similar(uint8_t macA[], uint8_t macB[], int *pn)
{
    size_t num_diff = 0; // Num hex digits which differ
    size_t idx_diff = 0; // nibble digit which differs
    size_t n;
    int result = 1;

    /* for each nibble, increment count if different */
    for (n = 0; n < MAC_SIZE * 2; n++) {
        if ((macA[n / 2] & NIBBLE_MASK(n)) != (macB[n / 2] & NIBBLE_MASK(n))) {
            if (++num_diff > 1)
                return 0;
            idx_diff = n;
            result = macA[n / 2] - macB[n / 2];
        }
    }

    /* Only one nibble different, but is the Local Administrative bit different */
    if (LOCAL_ADMIN_MASK(macA[0]) != LOCAL_ADMIN_MASK(macB[0])) {
        return 0; /* not similar */
    }

    /* report which nibble is different */
    if (pn)
        *pn = idx_diff;
    return result;
}

/*! \brief try to remove one AP by selecting an AP which leaves best spread of rssi values
 *
 *  Workspace AP beacons are stored in decreasing rssi order
 *  Avoid removing first and last beacons (with highest and lowest rssi) as this likely
 *  would reduce the overall range of rssi values.
 *
 *  @param ctx Skyhook request context
 *
 *  @return boot true if beacon removed or false
 */
static bool remove_worst_ap_by_rssi(Sky_ctx_t *ctx)
{
    int i, reject, jump, up_down;
    float band_range, worst, difference;
    float ideal_rssi[MAX_AP_BEACONS + 1];
    Beacon_t *b;

    if (NUM_APS(ctx) <= CONFIG(ctx->state, max_ap_beacons))
        return false;

    if (ctx->beacon[0].h.type != SKY_BEACON_AP)
        return false;

    /* what share of the range of rssi values does each beacon represent */
    band_range = (EFFECTIVE_RSSI(ctx->beacon[0].h.rssi) -
                     EFFECTIVE_RSSI(ctx->beacon[NUM_APS(ctx) - 1].h.rssi)) /
                 ((float)NUM_APS(ctx) - 1);

    /* if the rssi range is small
     * first, look for and remove an uncached *and* unconnected AP.
     * If there isn't one, look for and remove an uncached AP.
     * In either case, search from the middle outward in order to
     * avoid reducing an already small RSSI value range
     */

    if (band_range < 0.5) {
        /* search from middle of range looking for uncached and unconnected beacon */
        for (jump = 0, up_down = -1, i = NUM_APS(ctx) / 2; i >= 0 && i < NUM_APS(ctx);
             jump++, i += up_down * jump, up_down = -up_down) {
            b = &ctx->beacon[i];
            if (!b->ap.property.in_cache && !b->ap.h.connected) {
                LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small, %s beacon",
                    !jump ? "remove middle unconnected uncached" : "remove unconnected uncached");
                return remove_beacon(ctx, i) == SKY_SUCCESS;
            }
        }
        for (jump = 0, up_down = -1, i = NUM_APS(ctx) / 2; i >= 0 && i < NUM_APS(ctx);
             jump++, i += up_down * jump, up_down = -up_down) {
            b = &ctx->beacon[i];
            if (!b->ap.property.in_cache) {
                LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Warning: rssi range is small, %s beacon",
                    !jump ? "remove middle uncached" : "remove uncached");
                return remove_beacon(ctx, i) == SKY_SUCCESS;
            }
        }
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING,
            "Warning: rssi range is small, remove cached or connected beacon");
        return remove_beacon(ctx, NUM_APS(ctx) / 2) == SKY_SUCCESS;
    }

    /* if beacon with min RSSI is below threshold, */
    /* throw out a weak one */
    if (AP_BELOW_RSSI_THRESHOLD(ctx, NUM_APS(ctx) - 1)) {
        /* find weak ap not connected amd not in cache if possible */
        for (i = NUM_APS(ctx) - 1, reject = -1; i > 0 && reject == -1; i--) {
            if (AP_BELOW_RSSI_THRESHOLD(ctx, i) && !ctx->beacon[i].ap.h.connected &&
                !ctx->beacon[i].ap.property.in_cache)
                reject = i;
        }
        /* Second, if none found, try to find uncached, weak, AP. */
        for (i = NUM_APS(ctx) - 1, reject = -1; i > 0 && reject == -1; i--) {
            if (AP_BELOW_RSSI_THRESHOLD(ctx, i) && !ctx->beacon[i].ap.property.in_cache)
                reject = i;
        }
        /* Third, if none found, remove weakest AP. */
        if (reject == -1)
            reject = NUM_APS(ctx) - 1;
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Discarding beacon %d with very weak strength", reject);
        return remove_beacon(ctx, reject) == SKY_SUCCESS;
    }

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "range: %d band range: %d.%02d",
        (int)(band_range * (NUM_APS(ctx) - 1)), (int)band_range,
        (int)fabs(round(100 * (band_range - (int)band_range))));

    /* for each beacon, work out it's ideal rssi value to give an even distribution */
    for (i = 0; i < NUM_APS(ctx); i++)
        ideal_rssi[i] = EFFECTIVE_RSSI(ctx->beacon[0].h.rssi) - (i * band_range);

    /* find AP with poorest fit to ideal rssi */
    /* always keep lowest and highest rssi */
    /* unless all the middle candidates are connected or in the cache */
    for (i = 1, reject = -1, worst = 0; i < NUM_APS(ctx) - 1; i++) {
        difference = fabs(EFFECTIVE_RSSI(ctx->beacon[i].h.rssi) - ideal_rssi[i]);
        if (!ctx->beacon[i].ap.property.in_cache && !ctx->beacon[i].ap.h.connected &&
            difference >= worst) {
            worst = difference;
            reject = i;
        }
    }
    /* haven't found a beacon to remove yet due to matching cached beacons and connected */
    /* find poorest fit which may be in cache */
    if (reject == -1) {
        for (i = 1, worst = 0; i < NUM_APS(ctx) - 1; i++) {
            difference = fabs(EFFECTIVE_RSSI(ctx->beacon[i].h.rssi) - ideal_rssi[i]);
            if (!ctx->beacon[i].ap.h.connected && difference >= worst) {
                worst = difference;
                reject = i;
            }
        }
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons or connected */
        /* Throw away either lowest or highest rssi valued beacons if not cached */
        if (!ctx->beacon[NUM_APS(ctx) - 1].ap.property.in_cache)
            reject = NUM_APS(ctx) - 1;
        else if (!ctx->beacon[0].ap.property.in_cache)
            reject = 0;
        else
            reject = NUM_APS(ctx) / 2; /* remove middle beacon (all beacons are in cache) */
    }
#if SKY_DEBUG
    for (i = 0; i < NUM_APS(ctx); i++) {
        b = &ctx->beacon[i];
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "%-2d %s %s: %-2d, %s ideal %d.%02d fit %2d.%02d MAC %02X:%02X:%02X:%02X:%02X:%02X (%d)",
            i, b->ap.h.connected ? "*" : " ", (reject == i) ? "remove" : "      ", i,
            b->ap.property.in_cache ? "Cached" : "      ", (int)ideal_rssi[i],
            (int)fabs(round(100 * (ideal_rssi[i] - (int)ideal_rssi[i]))),
            (int)fabs(EFFECTIVE_RSSI(b->h.rssi) - ideal_rssi[i]), ideal_rssi[i],
            (int)fabs(round(100 * (fabs(EFFECTIVE_RSSI(b->h.rssi) - ideal_rssi[i]) -
                                      (int)fabs(EFFECTIVE_RSSI(b->h.rssi) - ideal_rssi[i])))),
            b->ap.mac[0], b->ap.mac[1], b->ap.mac[2], b->ap.mac[3], b->ap.mac[4], b->ap.mac[5],
            b->h.rssi);
    }
#endif
    return remove_beacon(ctx, reject) == SKY_SUCCESS;
}

#if CACHE_SIZE
/*! \brief count number of cached APs in workspace relative to a cacheline
 *
 *  @param ctx Skyhook request context
 *  @param cl the cacheline to count in, otherwise count in workspace
 *
 *  @return number of used APs or -1 for fatal error
 */
static int count_cached_aps_in_workspace(Sky_ctx_t *ctx, Sky_cacheline_t *cl)
{
    int num_aps_cached = 0;
    int j, i;
    if (!ctx || !cl)
        return -1;
    for (j = 0; j < NUM_APS(ctx); j++) {
        for (i = 0; i < NUM_APS(cl); i++) {
            num_aps_cached += equal(ctx, &ctx->beacon[j], &cl->beacon[i], NULL) ? 1 : 0;
        }
    }
#ifdef VERBOSE_DEBUG
    LOGFMT(
        ctx, SKY_LOG_LEVEL_DEBUG, "%d APs in cache %d", num_aps_cached, cl - ctx->state->cacheline);
#endif
    return num_aps_cached;
}
#endif

/*! \brief select between two virtual APs which should be removed,
 *  and then remove it
 *
 *  keep beacons with connectedness and then in_cache properties
 *
 *  @param ctx Skyhook request context
 *
 *  @return true if beacon removed or false otherwise
 */
static bool remove_poorest_of_pair(Sky_ctx_t *ctx, int i, int j)
{
    /* Assume we'll keep i and discard j. Then, apply following
     * logic to see if this should be reversed:
     * If j is connected and i is not, keep j
     * If i and j have the same connected state and j is in cache and i is not,
     * keep j.
     */
    int tmp;

    if ((ctx->beacon[j].ap.h.connected && !ctx->beacon[i].ap.h.connected) ||
        ((ctx->beacon[j].ap.h.connected == ctx->beacon[i].ap.h.connected) &&
            (ctx->beacon[j].ap.property.in_cache && !ctx->beacon[i].ap.property.in_cache))) {
        tmp = i;
        i = j;
        j = tmp;
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d similar to %d%s%s", j, i,
        ctx->beacon[i].ap.h.connected ? " (connected)" : "",
        ctx->beacon[i].ap.property.in_cache ? " (cached)" : "");
    return (remove_beacon(ctx, j) == SKY_SUCCESS);
}

/*! \brief try to reduce AP by filtering out virtual AP
 *
 *  When similar, remove beacon with highesr mac address
 *  unless it is in cache, then choose to remove the uncached beacon
 *
 *  @param ctx Skyhook request context
 *
 *  @return true if beacon removed or false otherwise
 */
static bool remove_virtual_ap(Sky_ctx_t *ctx)
{
    int i, j;
    int cmp;

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "ap_len: %d APs of %d beacons", (int)NUM_APS(ctx),
        (int)NUM_BEACONS(ctx));

    if (NUM_APS(ctx) <= CONFIG(ctx->state, max_ap_beacons)) {
        return false;
    }

    /* look for any AP beacon that is 'similar' to another */
    if (ctx->beacon[0].h.type != SKY_BEACON_AP) {
        LOGFMT(ctx, SKY_LOG_LEVEL_CRITICAL, "beacon type not WiFi");
        return false;
    }

#ifdef VERBOSE_DEBUG
    DUMP_WORKSPACE(ctx);
#endif
    /* Compare all beacons, looking for similar macs
     * Try to keep lowest of two beacons with similar macs, unless
     * the lower one is connected or in cache and the other is not
     */
    for (j = NUM_APS(ctx) - 1; j > 0; j--) {
        for (i = j - 1; i >= 0; i--) {
            if ((cmp = mac_similar(ctx->beacon[i].ap.mac, ctx->beacon[j].ap.mac, NULL)) < 0) {
                /* j has higher mac so we will remove it unless connected or in cache indicate otherwise
                 *
                 */
                return remove_poorest_of_pair(ctx, i, j);
            } else if (cmp > 0) {
                /* situation is exactly reversed (i has higher mac) but logic is otherwise
                 * identical
                 */
                return remove_poorest_of_pair(ctx, j, i);
            }
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "no match");
    return false;
}

/*! \brief try to reduce AP by filtering out the oldest one
 *
 *  Workspace AP beacons are stored in decreasing rssi order
 *
 *  @param ctx Skyhook request context
 *
 *  @return sky_status_t SKY_SUCCESS if beacon removed or SKY_ERROR
 */
static bool remove_worst_ap_by_age(Sky_ctx_t *ctx)
{
    int i;
    int oldest_idx = -1;
    uint32_t oldest_age = 0, youngest_age = UINT_MAX; /* age is in seconds, larger means older */

    if (NUM_APS(ctx) <= CONFIG(ctx->state, max_ap_beacons))
        return false;

    /* Find the youngest and oldest APs. Search from weakest remembering oldest and weakest */
    for (i = NUM_APS(ctx) - 1; i >= 0; i--) {
        if (ctx->beacon[i].h.age < youngest_age) {
            youngest_age = ctx->beacon[i].h.age;
        }
        if (ctx->beacon[i].h.age > oldest_age) {
            oldest_age = ctx->beacon[i].h.age;
            oldest_idx = i;
        }
    }

    /* if the oldest and youngest beacons have the same age,
     * there is nothing to do. Otherwise remove the oldest (and weakest) */
    if (youngest_age != oldest_age && oldest_idx != -1) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d oldest", oldest_idx);
        remove_beacon(ctx, oldest_idx);
        return true;
    }
    return false;
}

static Sky_status_t remove_worst(Sky_ctx_t *ctx)
{
    /* beacon is AP and is subject to filtering */
    /* discard virtual duplicates or remove one based on age or rssi distribution */
    if (!remove_virtual_ap(ctx) && !remove_worst_ap_by_age(ctx) && !remove_worst_ap_by_rssi(ctx)) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "failed to remove worst AP, try next plugin?");
        return SKY_ERROR;
    }
    return SKY_SUCCESS;
}

/*! \brief find cache entry with a match to workspace
 *
 *   Expire any old cachelines
 *   Compare each cacheline with the workspace beacons:
 *    . If workspace has enough cached APs, compare them with low threshold
 *    . If just a few APs, compare all APs with higher threshold
 *    . If no APs, compare cells for 100% match
 *
 *   If any cacheline score meets threshold, accept it.
 *   While searching, keep track of best cacheline to
 *   save a new server response. An empty cacheline is
 *   best, a good match is next, oldest is the fall back.
 *   Best cacheline to 'save_to' is set in the workspace for later use.
 *
 *  @param ctx Skyhook request context
 *  @param idx cacheline index of best match or empty cacheline or -1
 *
 *  @return index of best match or empty cacheline or -1
 */
static Sky_status_t match(Sky_ctx_t *ctx, int *idx)
{
#if CACHE_SIZE
    int i; /* i iterates through cacheline */
    int err; /* err breaks the seach due to bad value */
    float ratio; /* 0.0 <= ratio <= 1.0 is the degree to which workspace matches cacheline
                    In typical case this is the intersection(workspace, cache) / union(workspace, cache) */
    float bestratio = 0.0;
    float bestputratio = 0.0;
    int score; /* score is number of APs found in cacheline */
    int threshold; /* the threshold determined that ratio should meet */
    int num_aps_cached = 0;
    int bestc = -1, bestput = -1;
    int bestthresh = 0;
    Sky_cacheline_t *cl;
    bool result = false;

    if (!idx) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad parameter");
        return SKY_ERROR;
    }

    /* expire old cachelines and note first empty cacheline as best line to save to */
    for (i = 0, err = false; i < CACHE_SIZE; i++) {
        cl = &ctx->state->cacheline[i];
        /* if cacheline is old, mark it empty */
        if (cl->time != 0 && ((uint32_t)(*ctx->gettime)(NULL)-cl->time) >
                                 (CONFIG(ctx->state, cache_age_threshold) * SECONDS_IN_HOUR)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache line %d expired", i);
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

    if (NUM_APS(ctx) == 0) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "No APs");
        return SKY_ERROR;
    }

    DUMP_WORKSPACE(ctx);
    DUMP_CACHE(ctx);

    /* score each cache line wrt beacon match ratio */
    for (i = 0, err = false; i < CACHE_SIZE; i++) {
        cl = &ctx->state->cacheline[i];
        threshold = ratio = score = 0;
        if (cl->time == 0 || cell_changed(ctx, cl) == true) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "Cache: %d: Score 0 for empty cacheline or cell change", i);
            continue;
        } else {
            /* count number of matching APs in workspace and cache */
            if ((num_aps_cached = count_cached_aps_in_workspace(ctx, cl)) < 0) {
                err = true;
                break;
            } else if (NUM_APS(ctx) && NUM_APS(cl)) {
                /* Score based on ALL APs */
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score based on ALL APs", i);
                score = num_aps_cached;
                int unionAB = NUM_APS(ctx) + NUM_APS(cl) - num_aps_cached;
                threshold = CONFIG(ctx->state, cache_match_all_threshold);
                ratio = (float)score / unionAB;
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: score %d (%d/%d) vs %d", i,
                    (int)round(ratio * 100), score, unionAB, threshold);
                result = true;
            }
        }

        if (ratio > bestputratio) {
            bestput = i;
            bestputratio = ratio;
        }
        if (ratio > bestratio) {
            if (bestratio > 0.0)
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    "Found better match in cache %d of %d score %d (vs %d)", i, CACHE_SIZE,
                    (int)round(ratio * 100), threshold);
            bestc = i;
            bestratio = ratio;
            bestthresh = threshold;
        }
        if (ratio * 100 > threshold)
            break;
    }
    if (err) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad parameters counting APs");
        return SKY_ERROR;
    }

    /* make a note of the best match used by add_to_cache */
    ctx->save_to = bestput;

    if (result && bestratio * 100 > bestthresh) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "location in cache, pick cache %d of %d score %d (vs %d)",
            bestc, CACHE_SIZE, (int)round(bestratio * 100), bestthresh);
        *idx = bestc;
        return SKY_SUCCESS;
    }
    if (result) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "No Cache match found. Cache %d, best score %d (vs %d)",
            bestc, (int)round(bestratio * 100), bestthresh);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Best cacheline to save location: %d of %d score %d",
            bestput, CACHE_SIZE, (int)round(bestputratio * 100));
        return SKY_FAILURE;
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Unable to compare using APs. No cache match");
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Best cacheline to save location: %d of %d score %d", bestput,
        CACHE_SIZE, (int)round(bestputratio * 100));
    return SKY_ERROR;
#else
    (void)ctx; /* suppress warning unused parameter */
    (void)idx; /* suppress warning unused parameter */
    return SKY_FAILURE;
#endif
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
static Sky_status_t to_cache(Sky_ctx_t *ctx, Sky_location_t *loc)
{
#if CACHE_SIZE
    int i = ctx->save_to;
    int j;
    uint32_t now = (*ctx->gettime)(NULL);
    Sky_cacheline_t *cl;

    if (CACHE_SIZE < 1) {
        return SKY_SUCCESS;
    }

    /* compare current time to Mar 1st 2019 */
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Time (now) %d %d", now, time(NULL));
    if (now <= TIMESTAMP_2019_03_01) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Don't have good time of day! %u", now);
        return SKY_ERROR;
    }

    /* if best 'save-to' location was not set by beacon_score, use oldest */
    if (i < 0) {
        i = find_oldest(ctx);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "find_oldest chose cache %d of %d", i, CACHE_SIZE);
    }
    cl = &ctx->state->cacheline[i];
    if (loc->location_status != SKY_LOCATION_STATUS_SUCCESS) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "Won't add unknown location to cache");
        cl->time = 0; /* clear cacheline */
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "clearing cache %d of %d", i, CACHE_SIZE);
        return SKY_ERROR;
    } else if (cl->time == 0)
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Saving to empty cache %d of %d", i, CACHE_SIZE);
    else
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Saving to cache %d of %d", i, CACHE_SIZE);

    cl->len = NUM_BEACONS(ctx);
    cl->ap_len = NUM_APS(ctx);
    cl->loc = *loc;
    cl->time = now;

    for (j = 0; j < NUM_BEACONS(ctx); j++) {
        cl->beacon[j] = ctx->beacon[j];
        if (cl->beacon[j].h.type == SKY_BEACON_AP) {
            cl->beacon[j].ap.property.in_cache = true;
        }
    }
    DUMP_CACHE(ctx);
    return SKY_SUCCESS;
#else
    (void)ctx; /* suppress warning unused parameter */
    (void)loc; /* suppress warning unused parameter */
    return SKY_SUCCESS;
#endif
}

/* * * * * * Plugin access table * * * * *
 *
 * Each plugin is registered via the access table
 * The tables for each plugin are formed into a linked list
 *
 * For a given operation, each registered plugin is
 * called for that operation until a plugin returns success.
 */

Sky_plugin_table_t ap_plugin_basic_table = {
    .next = NULL, /* Pointer to next plugin table */
    .magic = SKY_MAGIC, /* Mark table so it can be validated */
    .name = __FILE__,
    /* Entry points */
    .equal = equal, /*Compare two beacons for equality */
    .remove_worst = remove_worst, /* Remove least desirable beacon from workspace */
    .cache_match = match, /* Find best match between workspace and cache lines */
    .add_to_cache = to_cache /* Copy workspace beacons to a cacheline */
};
