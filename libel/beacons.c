/*! \file libel/beacons.c
 *  \brief utilities - Skyhook Embedded Library
 *
 * Copyright 2015-present Skyhook Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include "../.submodules/tiny-AES128-C/aes.h"
#define SKY_LIBEL 1
#include "libel.h"

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
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Invalid params. Beacon type %s", sky_pbeacon(b))
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
                if (ctx->beacon[i].h.type != SKY_BEACON_AP || ctx->beacon[i].ap.rssi > b->ap.rssi)
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
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon type %s inserted idx: %d", sky_pbeacon(b), i);

    if (b->h.type == SKY_BEACON_AP)
        ctx->ap_len++;
    return SKY_SUCCESS;
}

/*! \brief try to reduce AP by filtering out based on diversity of rssi
 *
 *  @param ctx Skyhook request context
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static Sky_status_t filter_by_rssi(Sky_ctx_t *ctx)
{
    int i, reject;
    float band_range, worst;
    float ideal_rssi[MAX_AP_BEACONS + 1];

    if (ctx->ap_len <= MAX_AP_BEACONS)
        return SKY_ERROR;

    /* what share of the range of rssi values does each beacon represent */
    band_range =
        (ctx->beacon[ctx->ap_len - 1].ap.rssi - ctx->beacon[0].ap.rssi) / ((float)ctx->ap_len - 1);

    /* if the rssi range is small, throw away middle beacon */

    if (band_range < 0.5) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Warning: rssi range is small. Discarding one beacon...")
        return remove_beacon(ctx, ctx->ap_len / 2);
    }

    /* if beacon with min RSSI is below threshold, throw it out */
    if (ctx->beacon[0].ap.rssi < CACHE_RSSI_THRESHOLD) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Discarding beacon %d with very weak strength", 0)
        return remove_beacon(ctx, 0);
    }

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "range: %d band range: %d.%02d",
        (ctx->beacon[ctx->ap_len - 1].ap.rssi - ctx->beacon[0].ap.rssi), (int)band_range,
        (int)fabs(round(100 * (band_range - (int)band_range))))

    /* for each beacon, work out it's ideal rssi value to give an even distribution */
    for (i = 0; i < ctx->ap_len; i++)
        ideal_rssi[i] = ctx->beacon[0].ap.rssi + (i * band_range);

    /* find AP with poorest fit to ideal rssi */
    /* always keep lowest and highest rssi */
    /* unless all the middle candidates are in the cache */
    for (i = 1, reject = -1, worst = 0; i < ctx->ap_len - 1; i++) {
        if (!ctx->beacon[i].ap.in_cache && fabs(ctx->beacon[i].ap.rssi - ideal_rssi[i]) > worst) {
            worst = fabs(ctx->beacon[i].ap.rssi - ideal_rssi[i]);
            reject = i;
        }
    }
    if (reject == -1) {
        /* haven't found a beacon to remove yet due to matching cached beacons */
        /* Throw away either lowest or highest rssi valued beacons */
        if (!ctx->beacon[ctx->ap_len - 1].ap.in_cache && ctx->beacon[0].ap.in_cache)
            reject = ctx->ap_len - 1;
        else
            reject = 0; /* Throw away lowest rssi value */
    }
    for (i = 0; i < ctx->ap_len; i++) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%s: %-2d, %s ideal %d.%02d fit %2d.%02d (%d)",
            (reject == i) ? "remove" : "      ", i,
            ctx->beacon[i].ap.in_cache ? "cached" : "      ", (int)ideal_rssi[i],
            (int)fabs(round(100 * (ideal_rssi[i] - (int)ideal_rssi[i]))),
            (int)fabs(ctx->beacon[i].ap.rssi - ideal_rssi[i]), ideal_rssi[i],
            (int)fabs(round(100 * (fabs(ctx->beacon[i].ap.rssi - ideal_rssi[i]) -
                                      (int)fabs(ctx->beacon[i].ap.rssi - ideal_rssi[i])))),
            ctx->beacon[i].ap.rssi)
    }
    return remove_beacon(ctx, reject);
}

/*! \brief try to reduce AP by filtering out virtual AP
 *
 *  @param ctx Skyhook request context
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static Sky_status_t filter_virtual_aps(Sky_ctx_t *ctx)
{
    int i, j;
    int cmp;

    LOGFMT(
        ctx, SKY_LOG_LEVEL_DEBUG, "ap_len: %d APs of %d beacons", (int)ctx->ap_len, (int)ctx->len)

    dump_workspace(ctx);

    if (ctx->ap_len <= MAX_AP_BEACONS) {
        return SKY_ERROR;
    }

    /* look for any AP beacon that is 'similar' to another */
    if (ctx->beacon[0].h.type != SKY_BEACON_AP) {
        LOGFMT(ctx, SKY_LOG_LEVEL_CRITICAL, "beacon type not WiFi")
        return SKY_ERROR;
    }

    for (j = 0; j < ctx->ap_len; j++) {
        for (i = j + 1; i < ctx->ap_len; i++) {
            if ((cmp = similar(ctx->beacon[i].ap.mac, ctx->beacon[j].ap.mac)) < 0) {
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d similar to %d", j, i)
                remove_beacon(ctx, j);
                return SKY_SUCCESS;
            } else if (cmp > 0) {
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d similar to %d", i, j)
                remove_beacon(ctx, i);
                return SKY_SUCCESS;
            }
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "no match")
    return SKY_ERROR;
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

    /* don't add any more non-AP beacons if we've already hit the limit of non-AP beacons */
    if (b->h.type != SKY_BEACON_AP && ctx->len - ctx->ap_len > (TOTAL_BEACONS - MAX_AP_BEACONS)) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING, "too many (b->h.type: %s) (ctx->len - ctx->ap_len: %d)",
            sky_pbeacon(b), ctx->len - ctx->ap_len)
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);
    }

    /* insert the beacon */
    if (insert_beacon(ctx, sky_errno, b, &i) == SKY_ERROR)
        return SKY_ERROR;
    if (is_connected)
        ctx->connected = i;

    if (b->h.type == SKY_BEACON_AP)
        ctx->beacon[i].ap.in_cache = beacon_in_cache(ctx, b, ctx->cache->newest);
    else /* only filter APs */
        return sky_return(sky_errno, SKY_ERROR_NONE);

    /* done if no filtering needed */
    if (ctx->ap_len <= MAX_AP_BEACONS)
        return sky_return(sky_errno, SKY_ERROR_NONE);

    /* beacon is AP and is subject to filtering */
    if (filter_virtual_aps(ctx) == SKY_ERROR)
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

    if (!cl || !b || !ctx)
        return false;

    /* score each cache line wrt beacon match ratio */
    for (j = 0; ret == 0 && j < cl->len; j++)
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
    int i, j, start, end;
    float ratio[CACHE_SIZE];
    int score[CACHE_SIZE];
    float bestratio = 0;
    int bestscore = 0;
    int bestc = -1;

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%s", put ? "for save to cache" : "for get from cache")
    dump_cache(ctx);
    dump_workspace(ctx);

    /* score each cache line wrt beacon match ratio */
    for (i = 0; i < CACHE_SIZE; i++) {
        ratio[i] = score[i] = 0;
        if (put && (ctx->cache->cacheline[i].time == 0 ||
                       (uint32_t)(*ctx->gettime)(NULL)-ctx->cache->cacheline[i].time >
                           (CACHE_AGE_THRESHOLD * 60 * 60))) {
            /* looking for match for put, empty cacheline = 1st choice */
            score[i] = TOTAL_BEACONS * 2;
        } else if (!put && ctx->cache->cacheline[i].time == 0) {
            /* looking for match for get, ignore empty cache */
            continue;
        } else {
            /* Non empty cacheline - count matching beacons */
            if (ctx->ap_len) {
                start = 0;
                end = ctx->ap_len;
            } else {
                start = ctx->ap_len - 1;
                end = ctx->len;
            }

            for (j = start; j < end; j++) {
                if (beacon_in_cache(ctx, &ctx->beacon[j], &ctx->cache->cacheline[i])) {
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon %d type %s matches cache %d of 0..%d",
                        j, sky_pbeacon(&ctx->beacon[j]), i, CACHE_SIZE)
                    score[i] = score[i] + 1.0;
                }
            }
        }
    }

    for (i = 0; i < CACHE_SIZE; i++) {
        if (score[i] == TOTAL_BEACONS * 2) {
            ratio[i] = 1.0;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: fill empty cacheline", i)
        } else if (ctx->ap_len && ctx->cache->cacheline[i].ap_len) {
            // score = intersection(A, B) / union(A, B)

            ratio[i] = (float)score[i] / (ctx->ap_len + ctx->cache->cacheline[i].ap_len - score[i]);
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: score %d (%d/%d)", i,
                (int)round(ratio[i] * 100), score[i],
                ctx->ap_len + ctx->cache->cacheline[i].ap_len - score[i])

        } else if (ctx->len - ctx->ap_len &&
                   ctx->cache->cacheline[i].len - ctx->cache->cacheline[i].ap_len) {
            // if all cell beacons match
            ratio[i] = (score[i] == ctx->len - ctx->ap_len) ? 100.0 : 0.0;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: all %d cell beacons match",
                ctx->len - ctx->ap_len)
        }

        if (ratio[i] > bestratio) {
            bestratio = ratio[i];
            bestscore = score[i];
            bestc = i;
        }
    }

    /* if match is for get, must meet threshold */
    if (!put) {
        if (ctx->len <= CACHE_BEACON_THRESHOLD && bestscore == ctx->len) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Only %d beacons; pick cache %d of 0..%d score %d",
                ctx->len, bestc, CACHE_SIZE, (int)round(ratio[bestc] * 100))
            return bestc;
        } else if (ctx->len > CACHE_BEACON_THRESHOLD && bestratio * 100 > CACHE_MATCH_THRESHOLD) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "location in cache, pick cache %d of 0..%d score %d",
                bestc, CACHE_SIZE - 1, (int)round(ratio[bestc] * 100))
            return bestc;
        }
    } else if (bestc >= 0) { /* match is for put */
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "save location in best cache, %d of 0..%d score %d", bestc,
            CACHE_SIZE - 1, (int)round(ratio[bestc] * 100))
        return bestc;
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache match failed");
    return -1;
}

/*! \brief find cache entry with oldest entry
 *  if beacon is AP, filter
 *
 *  @param ctx Skyhook request context
 *
 *  @return index of oldest cache entry, or empty
 */
int find_oldest(Sky_ctx_t *ctx)
{
    int i;
    uint32_t oldestc = 0;
    int oldest = (*ctx->gettime)(NULL);

    for (i = 0; i < CACHE_SIZE; i++) {
        /* Discard old cachelines */
        if ((uint32_t)(*ctx->gettime)(NULL)-ctx->cache->cacheline[i].time >
            (CACHE_AGE_THRESHOLD * 60 * 60)) {
            ctx->cache->cacheline[i].time = 0;
            return i;
        }
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
    int newest = 0;

    for (i = 0; i < CACHE_SIZE; i++) {
        if (ctx->cache->cacheline[i].time > newest) {
            newest = ctx->cache->cacheline[i].time;
            ctx->cache->newest = &ctx->cache->cacheline[i];
        }
    }
    if (ctx->cache->newest)
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cacheline %d is newest",
            ctx->cache->newest - &ctx->cache->cacheline[0]);
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
    if ((i = find_best_match(ctx, 1)) < 0) {
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
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Won't add unknown location to cache")
        return SKY_ERROR;
    }
    ctx->cache->cacheline[i].len = ctx->len;
    ctx->cache->cacheline[i].ap_len = ctx->ap_len;
    ctx->cache->cacheline[i].loc = *loc;
    ctx->cache->cacheline[i].time = now;
    ctx->cache->newest = &ctx->cache->cacheline[i];
    for (j = 0; j < TOTAL_BEACONS; j++)
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
    return find_best_match(ctx, 0);
}
