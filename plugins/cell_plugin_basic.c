/*! \file plugins/premium_cell_plugin.c
 *  \brief Cell plugin supporting selection of best cells
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
#define SKY_LIBEL
#include "libel.h"

/* Uncomment VERBOSE_DEBUG to enable extra logging */
// #define VERBOSE_DEBUG

#define MIN(x, y) ((x) > (y) ? (y) : (x))

/*! \brief compare cell beacons fpr equality
 *
 *  if beacons are equivalent, return SKY_SUCCESS otherwise SKY_FAILURE
 *  if an error occurs during comparison. return SKY_ERROR
 */
static Sky_status_t equal(Sky_ctx_t *ctx, Beacon_t *a, Beacon_t *b, Sky_beacon_property_t *prop)
{
    (void)prop; /* suppress warning unused parameter */
    if (!ctx || !a || !b) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "bad params");
        return SKY_ERROR;
    }

    /* Two Cells can be compared but others are ordered by type */
    if (a->h.type != b->h.type || a->h.type == SKY_BEACON_AP || b->h.type == SKY_BEACON_AP ||
        a->h.type == SKY_BEACON_BLE || b->h.type == SKY_BEACON_BLE)
        return SKY_ERROR;

    /* test two cells for equivalence */
    switch (a->h.type) {
    case SKY_BEACON_CDMA:
        if ((a->cell.id2 == b->cell.id2) && (a->cell.id3 == b->cell.id3) &&
            (a->cell.id4 == b->cell.id4)) {
            if (!(a->cell.id2 == SKY_UNKNOWN_ID2 || a->cell.id3 == SKY_UNKNOWN_ID3 ||
                    a->cell.id4 == SKY_UNKNOWN_ID4))
                return SKY_SUCCESS;
        }
        break;
    case SKY_BEACON_GSM:
        if ((a->cell.id1 == b->cell.id1) && (a->cell.id2 == b->cell.id2) &&
            a->cell.id3 == b->cell.id3 && (a->cell.id4 == b->cell.id4)) {
            if (!((a->cell.id1 == SKY_UNKNOWN_ID1 || a->cell.id2 == SKY_UNKNOWN_ID2 ||
                    a->cell.id3 == SKY_UNKNOWN_ID3 || a->cell.id4 == SKY_UNKNOWN_ID4)))
                return SKY_SUCCESS;
        }
        break;
    case SKY_BEACON_LTE:
    case SKY_BEACON_NBIOT:
    case SKY_BEACON_UMTS:
    case SKY_BEACON_NR:
#ifdef VERBOSE_DEBUG
        dump_beacon(ctx, "a:", a, __FILE__, __FUNCTION__);
        dump_beacon(ctx, "b:", b, __FILE__, __FUNCTION__);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "LTE");
#endif
        if ((a->cell.id1 == b->cell.id1) && (a->cell.id2 == b->cell.id2) &&
            (a->cell.id4 == b->cell.id4)) {
            if ((a->cell.id1 == SKY_UNKNOWN_ID1) || (a->cell.id2 == SKY_UNKNOWN_ID2) ||
                (a->cell.id4 == SKY_UNKNOWN_ID4)) {
#ifdef VERBOSE_DEBUG
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "LTE-NMR");
#endif
                /* NMR */
                if ((a->cell.id5 == b->cell.id5) && (a->cell.freq == b->cell.freq))
                    return SKY_SUCCESS;
            } else
                return SKY_SUCCESS;
        }
        break;
    default:
        break;
    }
    return SKY_FAILURE;
}

/*! \brief remove least desirable cell if workspace is full
 *
 *  @param ctx Skyhook request context
 *
 *  @return sky_status_t SKY_SUCCESS if beacon removed or SKY_ERROR
 */
static Sky_status_t remove_worst(Sky_ctx_t *ctx)
{
    int i = NUM_BEACONS(ctx) - 1; /* index of last cell */
    Beacon_t *b = &ctx->beacon[i];

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%d cells present. Max %d", NUM_BEACONS(ctx) - NUM_APS(ctx),
        CONFIG(ctx->state, total_beacons) - CONFIG(ctx->state, max_ap_beacons));

    /* no work to do if workspace not full of max cell */
    if (NUM_CELLS(ctx) <= CONFIG(ctx->state, total_beacons) - CONFIG(ctx->state, max_ap_beacons)) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "No need to remove cell");
        return SKY_ERROR;
    }

    DUMP_WORKSPACE(ctx);

    /* sanity check last beacon, if we get here, it should be a cell */
    if (is_cell_type(b)) {
        /* cells are priority order
         * remove last beacon
         */
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d (least desirable cell)", i);
        return remove_beacon(ctx, i);
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Not a cell?");
    return SKY_ERROR;
}

/*! \brief find cache entry with a match to workspace
 *
 *   Expire any old cachelines
 *   Compare each cacheline with the workspace cell beacons:
 *    . compare cells for match using cells and NMR
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
    int bestc = -1, bestput = -1;
    int bestthresh = 0;
    Sky_cacheline_t *cl;
    bool result = false;

    DUMP_WORKSPACE(ctx);
    DUMP_CACHE(ctx);

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

    /* score each cache line wrt beacon match ratio */
    for (i = 0, err = false; i < CACHE_SIZE; i++) {
        cl = &ctx->state->cacheline[i];
        threshold = ratio = score = 0;
        if (cl->time == 0 || cell_changed(ctx, cl) == true) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "Cache: %d: Score 0 for empty cacheline or cell change", i);
            continue;
        } else {
            /* count number of matching cells */
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache: %d: Score based on cell beacons", i);
            threshold = CONFIG(ctx->state, cache_match_all_threshold);
            score = 0.0;
            for (int j = NUM_APS(ctx); j < NUM_BEACONS(ctx); j++) {
                if (beacon_in_cacheline(ctx, &ctx->beacon[j], &ctx->state->cacheline[i], NULL)) {
#ifdef VERBOSE_DEBUG
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                        "Cell Beacon %d type %s matches cache %d of %d Score %d", j,
                        sky_pbeacon(&ctx->beacon[j]), i, CACHE_SIZE, (int)score);
#endif
                    score = score + 1.0;
                }
            }
            /* score = number of matching cells */
            /* ratio is 1.0 when score == Number of cells */
            /* ratio if 0.0 when score is less than Number of cells */
            ratio = (float)score == NUM_CELLS(ctx) ? 1.0 : 0.0;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: score %d (%d/%d) vs %d", i,
                (int)round(ratio * 100), score, NUM_BEACONS(ctx), threshold);
            result = true;
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
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Bad parameters");
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
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Cache match failed. Cache %d, best score %d (vs %d)", bestc,
        (int)round(bestratio * 100), bestthresh);
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Best cacheline to save location: %d of %d score %d", bestput,
        CACHE_SIZE, (int)round(bestputratio * 100));
    return SKY_ERROR;
#else
    (void)ctx; /* suppress warning unused parameter */
    (void)idx; /* suppress warning unused parameter */
    return SKY_FAILURE;
#endif
}

/* * * * * * Plugin access table * * * * *
 *
 * Each plugin is registered via the access table
 * The tables for each plugin are formed into a linked list
 *
 * For a given operation, each registered plugin is
 * called for that operation until a plugin returns success.
 *
 * The supported operations are:
 *   name        - get name of plugin
 *   equal       - test two beacons for equivalence
 *   remove_worst - find least desirable beacon and remove it
 *   cache_match  - determine if cache has a good match
 *   add_to_cache - Save workspace in cache
 */

Sky_plugin_table_t cell_plugin_basic_table = {
    .next = NULL, /* Pointer to next plugin table */
    .magic = SKY_MAGIC, /* Mark table so it can be validated */
    .name = __FILE__,
    /* Entry points */
    .equal = equal, /*Compare two beacons for equality */
    .remove_worst = remove_worst, /* Remove least desirable beacon from workspace */
    .cache_match = match, /* Find best match between workspace and cache lines */
    .add_to_cache = NULL /* Copy workspace beacons to a cacheline */
};
